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
    struct percpu
    {
        struct compare
        {
            constexpr bool operator()(const std::shared_ptr<thread> &lhs, const std::shared_ptr<thread> &rhs) const
            {
                return lhs->vruntime < rhs->vruntime;
            }
        };

        lib::locker<
            lib::btree::multiset<
                std::shared_ptr<thread>,
                compare
            >, lib::rwspinlock_preempt
        > queue;
        std::shared_ptr<thread> running_thread;

        std::shared_ptr<process> idle_proc;
        std::shared_ptr<thread> idle_thread;

        lib::locker<
            std::list<
                std::shared_ptr<thread>
            >, lib::spinlock_preempt
        > dead_threads;
        lib::semaphore reap;
    };

    cpu_local<percpu> percpu;

    cpu_local<bool> preemption;
    cpu_local_init(preemption, true);

    cpu_local<bool> in_scheduler;
    cpu_local_init(in_scheduler, false);

    cpu_local_init(percpu);

    namespace arch
    {
        void init();
        void reschedule(std::size_t ms);

        void finalise(const std::shared_ptr<process> &proc, const std::shared_ptr<thread> &thread, std::uintptr_t ip, std::uintptr_t arg);
        void deinitialise(process *proc, thread *thread);

        void save(const std::shared_ptr<thread> &thread);
        void load(const std::shared_ptr<thread> &thread);
    } // namespace arch

    namespace
    {
        lib::locker<
            lib::map::flat_hash<
                std::size_t, std::shared_ptr<process>
            >, lib::rwspinlock
        > processes;

        bool initialised = false;

        std::size_t alloc_pid(std::shared_ptr<process> proc)
        {
            static std::atomic_size_t next_pid = 0;
            const auto pid = next_pid++;
            processes.write_lock().value()[pid] = proc;
            return pid;
        }

        void save(std::shared_ptr<thread> thread, cpu::registers *regs)
        {
            std::memcpy(&thread->regs, regs, sizeof(cpu::registers));
            arch::save(thread);
        }

        void load(bool same_pid, std::shared_ptr<thread> &thread, cpu::registers *regs)
        {
            std::memcpy(regs, &thread->regs, sizeof(cpu::registers));
            arch::load(thread);
            if (!same_pid)
                proc_for(thread->pid)->vmspace->pmap->load();
        }

        void idle()
        {
            ::arch::halt(true);
        }
    } // namespace

    bool is_initialised() { return initialised; }

    process *proc_for(std::size_t pid)
    {
        if (pid == static_cast<std::size_t>(-1))
            return percpu->idle_proc.get();
        return processes.read_lock()->at(pid).get();
    }

    std::uintptr_t thread::allocate_ustack(const std::shared_ptr<process> &proc)
    {
        // TODO: mmap
        auto &pmap = proc->vmspace->pmap;
        const auto vaddr = (proc->next_stack_top -= boot::ustack_size);
        if (const auto ret = pmap->map_alloc(vaddr, boot::ustack_size, vmm::pflag::rwu, vmm::page_size::small); !ret)
            lib::panic("could not map user thread stack: {}", magic_enum::enum_name(ret.error()));

        return vaddr + boot::ustack_size;
    }

    std::uintptr_t thread::allocate_kstack(const std::shared_ptr<process> &proc)
    {
        auto &pmap = proc->vmspace->pmap;
        auto vaddr = vmm::alloc_vpages(vmm::space_type::stack, boot::kstack_size / pmm::page_size);
        if (const auto ret = pmap->map_alloc(vaddr, boot::kstack_size, vmm::pflag::rw, vmm::page_size::small); !ret)
            lib::panic("could not map kernel thread stack: {}", magic_enum::enum_name(ret.error()));

        return vaddr + boot::kstack_size;
    }

    void thread::prepare_sleep(std::size_t ms)
    {
        sleep_ints = ::arch::int_switch_status(false);
        sleep_lock.lock();
        status = status::sleeping;

        if (ms)
            sleep_for = ms * 1'000'000;
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
            if (const auto ret = pmap->unmap_dealloc(bottom, size, vmm::page_size::small); !ret)
                lib::panic("could not unmap stack: {}", magic_enum::enum_name(ret.error()));
        };

        const auto &proc = proc_for(pid);
        const auto &pmap = proc->vmspace->pmap;
        if (is_user)
            unmap_stack(pmap, ustack_top, boot::ustack_size);
        unmap_stack(pmap, kstack_top, boot::kstack_size);

        arch::deinitialise(proc, this);

        lib::panic("TODO: thread {} deconstructor", tid);
    }

    std::shared_ptr<thread> thread::create(const std::shared_ptr<process> &parent, std::uintptr_t ip, std::uintptr_t arg)
    {
        auto thread = std::make_shared<sched::thread>();

        thread->tid = parent->next_tid++;
        thread->pid = parent->pid;
        thread->status = status::not_ready;
        thread->is_user = false;
        thread->priority = default_prio;
        thread->vruntime = 0;

        auto stack = thread::allocate_kstack(parent);
        thread->kstack_top = thread->ustack_top = stack;
        arch::finalise(parent, thread, ip, arg);

        const std::unique_lock _ { parent->lock };
        parent->threads[thread->tid] = thread;

        return thread;
    }

    process::~process()
    {
        lib::panic("TODO: process {} deconstructor", pid);
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
        else proc->root = proc->cwd = vfs::dentry::root(true);

        return proc;
    }

    thread *this_thread()
    {
        return percpu->running_thread.get();
    }

    std::size_t yield()
    {
        auto thread = this_thread();

        const bool eeping = thread->status == status::sleeping;
        const bool old = eeping ? thread->sleep_ints : ::arch::int_status();

        if (eeping && thread->sleep_for.has_value())
        {
            const auto clock = time::main_clock();
            thread->sleep_until = clock->ns() + thread->sleep_for.value();
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
            const auto size = obj.queue.read_lock()->size();
            if (size < min)
            {
                min = size;
                idx = i;
            }
        }
        return idx;
    }

    void enqueue(const std::shared_ptr<thread> &thread, std::size_t cpu_idx)
    {
        switch (thread->status)
        {
            [[likely]] case status::running:
                thread->status = status::ready;
                break;
            [[unlikely]] case status::killed:
            [[unlikely]] case status::not_ready:
                lib::panic(
                    "can't enqueue a thread that is {}",
                    magic_enum::enum_name(thread->status)
                );
                std::unreachable();
            case status::sleeping:
            case status::ready:
                break;
        }

        auto &obj = percpu.get(cpu::nth_base(cpu_idx));
        obj.queue.write_lock()->insert(thread);
    }

    void friends::spawn_on(std::size_t cpu, std::size_t pid, std::uintptr_t ip, std::uintptr_t arg)
    {
        const auto thread = thread::create(processes.read_lock()->at(pid), ip, arg);
        thread->status = status::ready;
        enqueue(thread, cpu);
    }

    void spawn(std::size_t pid, std::uintptr_t ip, std::uintptr_t arg)
    {
        spawn_on(allocate_cpu(), pid, ip, arg);
    }

    void spawn_on(std::size_t cpu, std::size_t pid, std::uintptr_t ip, std::uintptr_t arg)
    {
        friends::spawn_on(cpu, pid, ip, arg);
    }

    void reaper()
    {
        while (true)
        {
            percpu->reap.wait();

            // move dead_threads so it's not locked for a long time
            decltype(percpu::dead_threads)::value_type list;
            {
                auto locked = percpu->dead_threads.lock();
                list = std::move(locked.value());
                locked->clear();
            }
            while (!list.empty())
            {
                // TODO
                const auto thread = std::move(list.front());
                list.pop_front();
                lib::bug_if_not(thread.use_count() == 2);
                lib::bug_if_not(proc_for(thread->pid)->threads.erase(thread->tid) == 1);
                // thread deconstructor is called
            }
        }
        std::unreachable();
    }

    void schedule(cpu::registers *regs)
    {
        if (!preemption.get())
        {
            arch::reschedule(timeslice);
            return;
        }

        in_scheduler = true;

        const auto clock = time::main_clock();
        const auto time = clock->ns();

        const auto self = cpu::self();

        bool found_dead = false;

        std::shared_ptr<thread> next;
        {
            auto locked = percpu->queue.write_lock();
            for (auto it = locked->begin(); it != locked->end(); it++)
            {
                auto &thread = *it;
                switch (thread->status)
                {
                    case status::sleeping:
                        // TODO: handle wake up on a worker thread
                        if (!thread->sleep_until.has_value() || time < thread->sleep_until.value())
                            break;
                        thread->status = status::ready;
                        thread->wake_reason = wake_reason::success;
                        // do not starve out other threads
                        // if the woken one has been sleeping for too long
                        thread->vruntime = (*locked->begin())->vruntime;
                        [[fallthrough]];
                    case status::ready:
                        next = std::move(locked->extract(it).value());
                        goto found;
                    [[unlikely]] case status::running:
                        lib::panic("found a running thread in scheduler queue");
                        std::unreachable();
                    case status::killed:
                        percpu->dead_threads.lock()->push_back(thread);
                        locked->erase(it);
                        found_dead = true;
                        break;
                    [[unlikely]] case status::not_ready:
                        break;
                }
            }
            found:
        }

        const auto current = percpu->running_thread;
        const bool is_current_idle = (current == percpu->idle_thread);

        std::optional<std::size_t> prev_pid { };
        if (current) [[likely]]
        {
            if (current->status == status::killed)
            {
                percpu->dead_threads.lock()->push_back(current);
                found_dead = true;
            }
            else
            {
                prev_pid = current->pid;
                if (!is_current_idle) [[likely]]
                {
                    if (next == nullptr && current->status == status::running) [[unlikely]]
                        next = current;
                    else if (current->status == status::sleeping)
                        current->sleep_lock.unlock();

                    static constexpr std::size_t weight0 = prio_to_weight(0);
                    const std::size_t exec_time = time - current->schedule_time;
                    const std::size_t weight = prio_to_weight(current->priority);
                    const std::size_t vtime = (exec_time * weight0) / weight;
                    current->vruntime += vtime;

                    if (next != current) [[likely]]
                    {
                        save(current, regs);
                        enqueue(current, self->idx);
                    }
                }
                // should it save idle thread ctx?
                else save(current, regs);
            }
        }

        if (found_dead)
            percpu->reap.signal();

        if (next == nullptr) [[unlikely]]
            next = percpu->idle_thread;

        if (next != current) [[likely]]
        {
            next->running_on = self->self;
            next->status = status::running;
            percpu->running_thread = next;

            const bool same_pid = (prev_pid.has_value() && prev_pid.value() == next->pid);
            load(same_pid, next, regs);
        }

        if (!is_current_idle) [[likely]]
            next->schedule_time = clock->ns();

        arch::reschedule(timeslice);
        in_scheduler = false;
    }

    initgraph::stage *available_stage()
    {
        static initgraph::stage stage { "in-scheduler" };
        return &stage;
    }

    void friends::create_pid0()
    {
        auto proc = process::create(
            nullptr,
            std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
        );
        lib::bug_if_not(proc->pid == 0);
    }

    initgraph::task scheduler_task
    {
        "init-pid0",
        initgraph::require { ::arch::cpus_stage(), timers::available_stage() },
        initgraph::entail { available_stage() },
        friends::create_pid0
    };

    void enable()
    {
        if (initialised)
        {
            preemption = true;
            if (!in_scheduler.get())
                arch::reschedule(0);
        }
    }

    void disable()
    {
        if (initialised)
            preemption = false;
    }

    bool is_enabled()
    {
        return initialised && preemption.get();
    }

    void friends::start()
    {
        static std::atomic_bool should_start = false;

        static auto idle_vmspace = std::make_shared<vmm::vmspace>(
            std::make_shared<vmm::pagemap>(vmm::kernel_pagemap.get())
        );

        const auto self = cpu::self();

        const auto idle_proc = std::make_shared<process>();
        idle_proc->vmspace = idle_vmspace;
        idle_proc->pid = static_cast<std::size_t>(-1);

        const auto idle_thread = thread::create(idle_proc, reinterpret_cast<std::uintptr_t>(idle), 0);
        idle_thread->status = status::ready;

        percpu->idle_proc = idle_proc;
        percpu->idle_thread = idle_thread;

        arch::init();
        ::arch::int_switch(true);

        if (self->idx == cpu::bsp_idx())
        {
            for (std::size_t idx = 0; idx < cpu::cpu_count(); idx++)
                spawn_on(idx, 0, reinterpret_cast<std::uintptr_t>(reaper));

            initialised = true;
            should_start = true;
        }

        while (!should_start)
            ::arch::pause();

        arch::reschedule(0);
    }

    [[noreturn]] void start()
    {
        friends::start();
        ::arch::halt(true);
    }
} // namespace sched