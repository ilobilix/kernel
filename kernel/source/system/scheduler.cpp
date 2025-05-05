// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import drivers.timers;
import system.cpu.self;
import system.memory;
import system.time;
import system.acpi;
import magic_enum;
import boot;
import arch;
import lib;
import cppstd;

namespace sched
{
    cpu_local_init(percpu);

    namespace arch
    {
        void init();
        void reschedule(std::size_t ms);

        void finalise(std::shared_ptr<thread> &thread, std::uintptr_t ip);
        void deinitialise(std::shared_ptr<thread> &thread);

        void save(std::shared_ptr<thread> &thread);
        void load(std::shared_ptr<thread> &thread);
    } // namespace arch

    namespace
    {
        lib::map::flat_hash<std::size_t, std::shared_ptr<process>> processes;
        lib::spinlock<false> process_lock;

        std::size_t alloc_pid(std::shared_ptr<process> proc)
        {
            static std::atomic_size_t next_pid = 0;
            const std::unique_lock _ { process_lock };
            auto pid = next_pid++;
            processes[pid] = proc;
            return pid;
        }

        std::shared_ptr<thread> next_thread()
        {
            const std::unique_lock _ { percpu->lock };
            auto left = percpu->queue.begin();
            if (left == percpu->queue.end())
                return percpu->idle_thread;
            return std::move(percpu->queue.extract(left).value());
        }

        void save(std::shared_ptr<thread> thread, cpu::registers *regs)
        {
            std::memcpy(&thread->regs, regs, sizeof(cpu::registers));
            arch::save(thread);
        }

        void load(std::shared_ptr<thread> &thread, cpu::registers *regs)
        {
            std::memcpy(regs, &thread->regs, sizeof(cpu::registers));
            arch::load(thread);
            proc_for(thread->pid)->vmspace->pmap->load();
        }

        void idle()
        {
            ::arch::halt(true);
        }
    } // namespace

    std::shared_ptr<process> &proc_for(std::size_t pid)
    {
        if (pid == static_cast<std::size_t>(-1))
            return percpu->idle_proc;
        const std::unique_lock _ { process_lock };
        return processes.at(pid);
    }

    std::uintptr_t thread::allocate_ustack()
    {
        auto &parent = proc_for(pid);

        const auto vaddr = (parent->next_stack_top -= boot::ustack_size);

        const auto flags = vmm::flag::rw;
        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);
        const auto npages = lib::div_roundup(npsize, pmm::page_size);

        for (std::size_t i = 0; i < boot::ustack_size; i += npsize)
        {
            const auto paddr = pmm::alloc<std::uintptr_t>(npages, true);
            if (auto ret = vmm::kernel_pagemap->map(vaddr + i, paddr, npsize, flags, psize); !ret)
                lib::panic("could not map user thread stack: {}", magic_enum::enum_name(ret.error()));
        }

        return vaddr + boot::ustack_size;
    }

    std::uintptr_t thread::allocate_kstack()
    {
        auto vaddr = vmm::alloc_vpages(vmm::space_type::other, boot::kstack_size / pmm::page_size);

        const auto flags = vmm::flag::rw;
        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);
        const auto npages = lib::div_roundup(npsize, pmm::page_size);

        for (std::size_t i = 0; i < boot::kstack_size; i += npsize)
        {
            const auto paddr = pmm::alloc<std::uintptr_t>(npages, true);
            if (auto ret = vmm::kernel_pagemap->map(vaddr + i, paddr, npsize, flags, psize); !ret)
                lib::panic("could not map kernel thread stack: {}", magic_enum::enum_name(ret.error()));
        }

        return vaddr + boot::kstack_size;
    }

    std::shared_ptr<thread> thread::create(std::shared_ptr<process> &parent, std::uintptr_t ip)
    {
        auto thread = std::make_shared<sched::thread>();

        thread->tid = parent->next_tid++;
        thread->pid = parent->pid;
        thread->status = status::not_ready;
        thread->is_user = false;
        thread->vruntime = 0;

        auto stack = thread->allocate_kstack();
        thread->kstack_top = thread->ustack_top = stack;
        arch::finalise(thread, ip);

        const std::unique_lock _ { parent->lock };
        parent->threads[thread->tid] = thread;
        // enqueued manually

        return thread;
    }

    void thread::prepare_sleep(std::size_t ms)
    {
        sleep_ints = ::arch::int_switch_status(false);
        sleep_lock.lock();
        status = status::sleeping;

        if (ms)
            sleep_for = ms;
        else
            sleep_for = std::nullopt;
    }

    bool thread::wake_up(std::size_t reason)
    {
        const bool ints = ::arch::int_switch_status(false);
        sleep_lock.lock();

        if (status != status::sleeping)
        {
            sleep_lock.unlock();
            ::arch::int_switch(ints);
            return false;
        }

        status = status::ready;
        wake_reason = reason;

        sleep_lock.unlock();
        ::arch::int_switch(ints);
        return true;
    }

    thread::~thread()
    {
        auto unmap_stack = [](auto &pmap, std::uintptr_t top, std::size_t size)
        {
            const std::uintptr_t bottom = top - size;
            for (std::size_t i = 0; i < size; i += pmm::page_size)
            {
                const auto vaddr = bottom + i;
                const auto ret = pmap->translate(vaddr);
                if (!ret.has_value())
                    lib::panic("could not unmap user stack");
                pmm::free(ret.value());
            }
        };
        if (is_user)
        {
            auto &pmap = proc_for(pid)->vmspace->pmap;
            unmap_stack(pmap, ustack_top, boot::ustack_size);
        }
        unmap_stack(vmm::kernel_pagemap, kstack_top, boot::kstack_size);

        lib::panic("TODO: thread {} deconstructor", tid);
    }

    std::shared_ptr<process> process::create(std::shared_ptr<process> parent, std::shared_ptr<vmm::pagemap> pagemap)
    {
        auto proc = std::make_shared<process>();
        proc->pid = alloc_pid(proc);
        proc->vmspace = std::make_shared<vmm::vmspace>(pagemap);

        if (parent)
        {
            proc->root = parent->root;
            proc->cwd = parent->cwd;
            proc->parent = parent;

            const std::unique_lock _ { parent->lock };
            parent->children[proc->pid] = proc;
        }
        else proc->root = proc->cwd = vfs::node::root(true);

        return proc;
    }

    process::~process()
    {
        lib::panic("TODO: process {} deconstructor", pid);
    }

    std::shared_ptr<thread> this_thread()
    {
        return percpu->running_thread;
    }

    std::size_t yield()
    {
        auto thread = this_thread();

        const bool eeping = thread->status == status::sleeping;
        const bool old = eeping ? thread->sleep_ints : ::arch::int_status();

        if (eeping && thread->sleep_for.has_value())
        {
            const auto clock = time::main_clock();
            thread->sleep_until = clock->ns() / 1'000'000 + thread->sleep_for.value();
            thread->sleep_for = std::nullopt;
        }

        arch::reschedule(0);
        ::arch::int_switch(old);
        return eeping ? thread->wake_reason : wake_reason::success;
    }

    std::size_t allocate_cpu()
    {
        std::size_t idx = 0;
        std::size_t min = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < cpu::cpu_count(); i++)
        {
            auto &obj = percpu.get(cpu::nth_base(i));
            obj.lock.lock();
            const auto size = obj.queue.size();
            obj.lock.unlock();
            if (size < min)
            {
                min = size;
                idx = i;
            }
        }
        return idx;
    }

    void enqueue(std::shared_ptr<thread> &thread, std::size_t cpu_idx)
    {
        if (thread->status == status::running)
            thread->status = status::ready;

        auto &obj = percpu.get(cpu::nth_base(cpu_idx));
        const std::unique_lock _ { obj.lock };
        obj.queue.insert(thread);
    }

    void schedule(cpu::registers *regs)
    {
        const auto clock = time::main_clock();
        const auto time = clock->ns();

        const auto self = cpu::self();

        auto current = percpu->running_thread;
        if (current && current->status != status::killed)
        {
            if (current != percpu->idle_thread)
            {
                save(current, regs);

                if (current->status == status::sleeping)
                    current->sleep_lock.unlock();

                current->vruntime += time - current->schedule_time;
                enqueue(current, self->idx);
            }
        }
        // TODO: if killed

        auto first = next_thread();
        auto next = first;
        while (next->status != status::ready)
        {
            if (next->status == status::sleeping)
            {
                if (next->sleep_until.has_value() && time / 1'000'000 >= next->sleep_until.value())
                {
                    next->status = status::ready;
                    next->wake_reason = wake_reason::success;
                    break;
                }
            }
            enqueue(next, self->idx);
            next = next_thread();
            if (next == first)
            {
                enqueue(next, self->idx);
                next = percpu->idle_thread;
            }
        }

        percpu->running_thread = next;
        next->schedule_time = time;
        next->status = status::running;

        load(next, regs);
        arch::reschedule(timeslice);
    }

    initgraph::stage *available_stage()
    {
        static initgraph::stage stage { "in-scheduler" };
        return &stage;
    }

    initgraph::task scheduler_task
    {
        "init-pid0",
        initgraph::require { ::arch::cpus_stage(), timers::available_stage() },
        initgraph::entail { available_stage() },
        [] {
            auto proc = process::create(
                nullptr,
                std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
            );
            lib::ensure(proc->pid == 0);
        }
    };

    [[noreturn]] void start()
    {
        static std::atomic_bool should_start = false;

        static auto idle_vmspace = std::make_shared<vmm::vmspace>(
            std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
        );

        auto idle_proc = std::make_shared<process>();
        idle_proc->vmspace = idle_vmspace;
        idle_proc->pid = static_cast<std::size_t>(-1);

        auto idle_thread = thread::create(idle_proc, reinterpret_cast<std::uintptr_t>(idle));
        idle_thread->status = status::ready;

        percpu->idle_proc = idle_proc;
        percpu->idle_thread = idle_thread;

        arch::init();
        ::arch::int_switch(true);

        if (cpu::self()->idx == cpu::bsp_idx())
        {
            should_start = true;
            initialised = true;
        }

        while (!should_start)
            ::arch::pause();

        arch::reschedule(0);
        ::arch::halt(true);
    }
} // namespace sched