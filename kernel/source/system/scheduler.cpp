// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import drivers.timers;
import system.cpu.self;
import system.memory;
import system.time;
import system.acpi;
import magic_enum;
import frigg;
import boot;
import arch;
import lib;
import cppstd;

namespace sched
{
    class percpu
    {
        private:
        template<typename MType, MType thread::*Member>
        class compare
        {
            public:
            bool operator()(const thread &lhs, const thread &rhs) const
            {
                return lhs.*Member < rhs.*Member;
            }
        };

        public:
        lib::locker<
            lib::rbtree<
                thread,
                &thread::rbtree_hook,
                compare<
                    std::uint64_t,
                    &thread::vruntime
                >
            >,
            lib::spinlock_preempt
        > queue;

        thread *running_thread;

        lib::locker<
            lib::rbtree<
                thread,
                &thread::rbtree_hook,
                compare<
                    std::optional<std::size_t>,
                    &thread::sleep_until
                >
            >,
            lib::spinlock_preempt
        > sleep_queue;

        process *idle_proc;
        thread *idle_thread;

        lib::locker<
            frg::intrusive_list<
                thread,
                frg::locate_member<
                    thread,
                    frg::default_list_hook<thread>,
                    &thread::list_hook
                >
            >,
            lib::spinlock_preempt
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

        void finalise(process *proc, thread *thread, std::uintptr_t ip);
        void deinitialise(process *proc, thread *thread);

        void save(thread *thread);
        void load(thread *thread);
    } // namespace arch

    namespace
    {
        lib::locker<
            lib::map::flat_hash<
                std::size_t, process *
            >, lib::rwspinlock
        > processes;

        bool initialised = false;

        std::size_t alloc_pid(process *proc)
        {
            static std::atomic_size_t next_pid = 0;
            const auto pid = next_pid++;
            processes.write_lock().value()[pid] = proc;
            return pid;
        }

        void save(thread *thread, cpu::registers *regs)
        {
            std::memcpy(&thread->regs, regs, sizeof(cpu::registers));
            arch::save(thread);
        }

        void load(bool same_pid, thread *thread, cpu::registers *regs)
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
            return percpu->idle_proc;
        return processes.read_lock()->at(pid);
    }

    std::uintptr_t thread::allocate_ustack(process *proc)
    {
        // TODO: mmap
        auto &pmap = proc->vmspace->pmap;
        const auto vaddr = (proc->next_stack_top -= boot::ustack_size);
        if (const auto ret = pmap->map_alloc(vaddr, boot::ustack_size, vmm::pflag::rwu, vmm::page_size::small); !ret)
            lib::panic("could not map user thread stack: {}", magic_enum::enum_name(ret.error()));

        return vaddr + boot::ustack_size;
    }

    std::uintptr_t thread::allocate_kstack(process *proc)
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

    thread *thread::create(process *parent, std::uintptr_t ip)
    {
        auto thread = new sched::thread { };

        thread->tid = parent->next_tid++;
        thread->pid = parent->pid;
        thread->status = status::not_ready;
        thread->is_user = false;
        thread->priority = default_prio;
        thread->vruntime = 0;

        auto stack = thread::allocate_kstack(parent);
        thread->kstack_top = thread->ustack_top = stack;
        arch::finalise(parent, thread, ip);

        const std::unique_lock _ { parent->lock };
        parent->threads[thread->tid] = thread;

        return thread;
    }

    process::~process()
    {
        lib::panic("TODO: process {} deconstructor", pid);
    }

    process *process::create(process *parent, std::shared_ptr<vmm::pagemap> pagemap)
    {
        auto proc = new process { };

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
        return percpu->running_thread;
    }

    std::size_t sleep_for(std::size_t ms)
    {
        this_thread()->prepare_sleep(ms);
        return yield();
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
            const auto size = obj.queue.lock()->size();
            if (size < min)
            {
                min = size;
                idx = i;
            }
        }
        return idx;
    }

    void enqueue(thread *thread, std::size_t cpu_idx)
    {
        auto &obj = percpu.get(cpu::nth_base(cpu_idx));
        switch (thread->status)
        {
            [[unlikely]] case status::killed:
            [[unlikely]] case status::not_ready:
                lib::panic(
                    "can't enqueue a thread that is {}",
                    magic_enum::enum_name(thread->status)
                );
                std::unreachable();
            case status::sleeping:
                obj.sleep_queue.lock()->insert(thread);
                break;
            [[likely]] case status::running:
                thread->status = status::ready;
                [[fallthrough]];
            case status::ready:
                obj.queue.lock()->insert(thread);
                break;
            default:
                std::unreachable();
        }
    }

    void friends::spawn_on(std::size_t cpu, std::size_t pid, std::uintptr_t ip, nice_t priority)
    {
        const auto thread = thread::create(processes.read_lock()->at(pid), ip);
        thread->priority = priority;
        thread->status = status::ready;
        enqueue(thread, cpu);
    }

    void spawn(std::size_t pid, std::uintptr_t ip, nice_t priority)
    {
        spawn_on(allocate_cpu(), pid, ip, priority);
    }

    void spawn_on(std::size_t cpu, std::size_t pid, std::uintptr_t ip, nice_t priority)
    {
        friends::spawn_on(cpu, pid, ip, priority);
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
                while (!locked->empty())
                    list.push_back(locked->pop_front());
            }
            while (!list.empty())
            {
                // TODO
                const auto thread = std::move(list.front());
                list.pop_front();
                auto proc = proc_for(thread->pid);
                lib::bug_on(proc->threads.erase(thread->tid) != 1);
                delete thread;
                // TODO: orphan processes
            }
        }
        std::unreachable();
    }

    void sleeper()
    {
        while (true)
        {
            while (true)
            {
                if (!percpu->sleep_queue.lock()->empty())
                    break;
                yield();
            }

            const auto clock = time::main_clock();
            const auto time = clock->ns();

            bool found_dead = false;

            auto slocked = percpu->sleep_queue.lock();
            const auto begin = slocked->begin();
            for (auto it = begin; it != slocked->end(); )
            {
                const auto thread = (it++).value();
                if (!thread->sleep_until.has_value())
                {
                    switch (thread->status)
                    {
                        case status::ready:
                            slocked->remove(thread);
                            percpu->queue.lock()->insert(thread);
                            break;
                        case status::killed:
                            slocked->remove(thread);
                            percpu->dead_threads.lock()->push_back(thread);
                            found_dead = true;
                            break;
                        case status::not_ready:
                        case status::sleeping:
                            continue;
                        case status::running:
                            lib::panic("found a running thread in sleep queue");
                            std::unreachable();
                        default:
                            std::unreachable();
                    }
                }
                else
                {
                    if (time < thread->sleep_until.value())
                        break;

                    slocked->remove(thread);

                    thread->status = status::ready;
                    thread->wake_reason = wake_reason::success;
                    {
                        // do not starve out other threads
                        // if the woken one has been sleeping for too long
                        auto qlocked = percpu->queue.lock();
                        thread->vruntime = begin->vruntime;
                        qlocked->insert(thread);
                    }
                }
            }
            if (found_dead)
                percpu->reap.signal();
            yield();
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
        lib::bug_on(percpu->dead_threads.is_locked());
        lib::bug_on(percpu->sleep_queue.is_locked());

        in_scheduler = true;

        const auto clock = time::main_clock();
        const auto time = clock->ns();

        const auto self = cpu::self();

        bool found_dead = false;

        thread *next = nullptr;
        {
            auto locked = percpu->queue.lock();

            const auto begin = locked->begin();
            for (auto it = begin; it != locked->end(); )
            {
                const auto thread = (it++).value();
                switch (thread->status)
                {
                    case status::ready:
                        next = thread;
                        locked->remove(thread);
                        goto found;
                    [[unlikely]] case status::sleeping:
                    [[unlikely]] case status::running:
                        lib::panic(
                            "found a {} thread in scheduler queue",
                            magic_enum::enum_name(thread->status)
                        );
                        std::unreachable();
                    case status::killed:
                        locked->remove(thread);
                        percpu->dead_threads.lock()->push_back(thread);
                        found_dead = true;
                        break;
                    [[unlikely]] case status::not_ready:
                        break;
                    default:
                        std::unreachable();
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
        lib::bug_on(proc->pid != 0);
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

        const auto idle_proc = new process { };
        idle_proc->vmspace = idle_vmspace;
        idle_proc->pid = static_cast<std::size_t>(-1);

        const auto idle_thread = thread::create(idle_proc, reinterpret_cast<std::uintptr_t>(idle));
        idle_thread->status = status::ready;

        percpu->idle_proc = idle_proc;
        percpu->idle_thread = idle_thread;

        arch::init();
        ::arch::int_switch(true);

        if (self->idx == cpu::bsp_idx())
        {
            for (std::size_t idx = 0; idx < cpu::cpu_count(); idx++)
            {
                sched::spawn_on(idx, 0, reinterpret_cast<std::uintptr_t>(reaper), nice_t::max);
                sched::spawn_on(idx, 0, reinterpret_cast<std::uintptr_t>(sleeper), -5);
            }

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