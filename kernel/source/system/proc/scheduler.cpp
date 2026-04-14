// Copyright (C) 2024-2026  ilobilo

module system.sched;

import drivers.timers;
import system.chrono;
import system.cpu;
import arch;

namespace sched
{
    namespace arch
    {
        using namespace ::arch;
    } // namespace arch

    namespace
    {
        cpu_local(run_queue_t, run_queue);

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, lib::spinlock
        > processes;

        std::atomic_bool should_start = false;

        lib::locker<
            lib::rbtree<
                sleep_entry_t,
                &sleep_entry_t::hook,
                lib::compare<
                    sleep_entry_t,
                    std::uint64_t,
                    &sleep_entry_t::deadline_ns
                >
            >, lib::spinlock_irq
        > sleep_list;

        std::atomic<pid_t> next_id = 2;
        pid_t alloc_id()
        {
            const auto ret = next_id.fetch_add(1, std::memory_order_relaxed);
            lib::panic_if(
                ret == std::numeric_limits<pid_t>::max(),
                "implement a proper pid allocator"
            );
            return ret;
        }

        void free_id(pid_t id)
        {
            // TODO
            lib::unused(id);
        }

        bool can_run_on(const thread_t *thread, std::size_t cpu)
        {
            return thread->affinity.get(cpu);
        }

        std::size_t find_least_loaded(const thread_t *thread)
        {
            auto &self = cpu::self().unsafe_get();
            auto best_cpu = self.idx;
            auto best_load = std::numeric_limits<std::size_t>::max();

            for (std::size_t i = 0; i < cpu::count(); i++)
            {
                if (!can_run_on(thread, i))
                    continue;

                auto &rq = run_queue.unsafe_get(cpu::local::nth_base(i));
                if (rq.nr_running == 0)
                {
                    if (best_load != 0 || i == self.idx)
                    {
                        best_cpu = i;
                        best_load = 0;
                    }
                    continue;
                }

                std::uint64_t load = rq.total_weight;
                if (rq.current && !rq.current->is_idle())
                    load += rq.current->weight;

                if (load < best_load)
                {
                    best_load = load;
                    best_cpu = i;
                }
            }

            return best_cpu;
        }

        void enqueue_on(thread_t *thread, std::size_t cpu_idx, bool initial)
        {
            auto &self = cpu::self().unsafe_get();
            auto &rq = (cpu_idx == self.idx)
                ? run_queue.unsafe_get()
                : run_queue.unsafe_get(cpu::local::nth_base(cpu_idx));

            const std::unique_lock _ { rq.lock };

            rq.adjust(thread, initial);
            rq.enqueue(thread);
            rq.nr_running++;

            bool should_resched = false;
            if (rq.current)
            {
                if (rq.current->is_idle())
                    should_resched = true;
                else if (rq.check_preempt_wakeup(thread))
                    should_resched = true;
            }

            if (should_resched)
            {
                rq.current->flags |= thread_flags::needs_resched;
                if (cpu_idx != self.idx)
                    arch::wake_up_other(cpu_idx);
            }
        }

        std::uintptr_t allocate_kstack()
        {
            return lib::allocz<std::uintptr_t>(kstack_size);
        }

        lib::bitmap create_affinity()
        {
            lib::bitmap ret { cpu::count() };
            ret.clear(0xFF);
            return ret;
        }
    } // namespace

    lib::initgraph::stage *pid0_created_stage()
    {
        static lib::initgraph::stage stage
        {
            "sched.pid0.created",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task init_task
    {
        "log.create-thread",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            arch::bsp_initialised_stage(),
            timers::initialised_stage()
        },
        lib::initgraph::entail { pid0_created_stage() },
        [] {
            auto proc = new process_t { };

            proc->pid = 0;
            proc->parent = nullptr;
            proc->group = nullptr;
            proc->session = nullptr;

            proc->vmspace = std::make_shared<vmm::vmspace>(
                std::make_shared<vmm::pagemap>(
                    vmm::kernel_pagemap.get()
                )
            );

            proc->vfs = nullptr;
            proc->fdt = nullptr;
            proc->cred = std::make_shared<cred_t>();
            proc->sigactions = nullptr;

            (*processes.lock())[proc->pid] = proc;
        }
    };

    [[noreturn]] void start()
    {
        auto &self = cpu::self().unsafe_get();
        auto &rq = run_queue.unsafe_get();

        rq.cpu_idx = self.idx;
        rq.load_update = (self.idx * balance_interval_ns) / cpu::count();

        rq.idle = create_kthread(reinterpret_cast<std::uintptr_t>(arch::halt), true);
        rq.idle->flags |= thread_flags::idle;
        rq.idle->affinity.clear(0);
        rq.idle->affinity.set(rq.cpu_idx, true);

        rq.current = rq.idle;
        rq.current->running_on = self.self;
        rq.current->state = thread_state::running;

        arch::init_core(rq.current);

        if (rq.cpu_idx == cpu::bsp_idx())
        {
            running = true;
            should_start.store(true, std::memory_order_release);
        }
        else
        {
            while (!should_start.load(std::memory_order_acquire))
                arch::pause();
        }

        arch::int_switch(true);
        arch::arm_timer_ns(0);
        arch::halt(true);
    }

    void schedule()
    {
        preempt_disable();
        auto &self = cpu::self().unsafe_get();
        auto &rq = run_queue.unsafe_get();
        rq.lock.lock();
        preempt_enable();

        const auto prev = rq.current;
        thread_t *next = nullptr;

        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        rq.update_current(now);

        switch (prev->state)
        {
            case thread_state::running:
                prev->state = thread_state::runnable;
                if (!prev->in_rq && !prev->is_idle())
                    rq.enqueue(prev);
                break;
            case thread_state::runnable:
                break;
            case thread_state::sleeping:
            case thread_state::blocked:
            case thread_state::stopped:
                if (prev->in_rq)
                    rq.dequeue(prev);
                if (!prev->is_idle())
                    rq.nr_running--;
                break;
            case thread_state::zombie:
            case thread_state::dead:
                if (prev->in_rq)
                    rq.dequeue(prev);
                rq.current = nullptr;
                if (!prev->is_idle())
                    rq.nr_running--;
                break;
        }

        prev->flags &= ~thread_flags::needs_resched;

        next = rq.pick_next();
        if (next == nullptr)
        {
            rq.lock.unlock();
            load_balance();
            rq.lock.lock();
            next = rq.pick_next();
        }

        if (next == nullptr)
            next = rq.idle;
        else
            rq.dequeue(next);

        lib::bug_on(!can_run_on(next, rq.cpu_idx));

        next->state = thread_state::running;
        next->sched_time = now;
        next->prev_runtime = next->total_runtime;
        next->running_on = self.self;

        rq.current = next;
        rq.nr_switches++;

        const auto timeslice = rq.calc_timeslice(rq.current->weight);

        if (next == prev)
        {
            arch::arm_timer_ns(timeslice);
            rq.lock.unlock();
            return;
        }
        else if (next->proc != prev->proc)
            next->proc->vmspace->pmap->load();

        if (self.in_interrupt.load(std::memory_order_relaxed))
            next->was_in_interrupt = &self.in_interrupt;
        next->needs_unlock = &rq.lock;

        arch::arm_timer_ns(timeslice);
        arch::context_switch(prev, next);

        auto thread = current_thread();
        if (thread->needs_unlock)
        {
            thread->needs_unlock->unlock();
            thread->needs_unlock = nullptr;
        }
        if (thread->was_in_interrupt)
        {
            thread->was_in_interrupt->store(false, std::memory_order_release);
            thread->was_in_interrupt = nullptr;
        }
    }

    process_t *create_process(process_t *parent)
    {
        auto proc = new process_t { };

        if (parent == nullptr)
        {
            proc->pid = 1;
            proc->parent = proc;

            proc->session = new session_t { };
            proc->session->sid = proc->pid;
            proc->session->leader = proc;

            proc->group = new group_t { };
            proc->group->pgid = proc->pid;
            proc->group->session = proc->session;
            proc->group->leader = proc;

            proc->session->foreground_pg = proc->group;

            (*proc->group->members.lock())[proc->pid] = proc;
            (*proc->session->members.lock())[proc->group->pgid] = proc->group;
        }
        else
        {
            proc->pid = alloc_id();
            proc->parent = parent;

            proc->session = proc->parent->session;
            proc->group = proc->parent->group;

            (*proc->group->members.lock())[proc->pid] = proc;
        }

        lib::bug_on(get_process(proc->pid));
        (*processes.lock())[proc->pid] = proc;

        // the caller will set up the rest of the fields
        return proc;
    }

    thread_t *create_kthread(std::uintptr_t ip, std::uintptr_t arg, nice_t nice)
    {
        auto proc = get_process(0);
        lib::bug_on(!proc);

        auto thread = new thread_t { };
        thread->self = thread;

        thread->tid = alloc_id();
        thread->proc = proc;
        thread->flags = thread_flags::kernel;

        thread->nice = nice;
        thread->weight = nice_to_weight(thread->nice);
        thread->inv_weight = nice_to_inv_weight(thread->nice);

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        thread->affinity = create_affinity();

        arch::init_thread(thread, ip, arg, false);

        (*proc->threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    thread_t *create_uthread(
        process_t *proc, std::uintptr_t ip, std::uintptr_t arg, bool is_trampoline,
        std::uintptr_t stack, nice_t nice
    )
    {
        lib::bug_on(!proc);

        auto thread = new thread_t { };
        thread->self = thread;

        if (auto locked = proc->threads.lock(); locked->empty())
            thread->tid = proc->pid;
        else
            thread->tid = alloc_id();

        thread->proc = proc;

        thread->nice = nice;
        thread->weight = nice_to_weight(thread->nice);
        thread->inv_weight = nice_to_inv_weight(thread->nice);

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        if (stack == 0)
        {
            thread->ustack_top = proc->vmspace->alloc_stack_top(ustack_size);
            thread->ustack_base = thread->ustack_top - ustack_size;

            const auto prot = vmm::read | vmm::write;
            const auto flags = vmm::private_ | vmm::anonymous | vmm::fixed_noreplace;

            const auto ret = proc->vmspace->map(
                thread->ustack_base, ustack_size,
                prot, prot, flags, nullptr, 0
            );

            if (!ret)
            {
                lib::error("sched: could not map user stack");
                delete thread;
                return nullptr;
            }

            // TODO: guard page
        }
        else
        {
            thread->ustack_top = stack;
            thread->ustack_base = 0;
        }

        thread->affinity = create_affinity();

        arch::init_thread(thread, ip, arg, is_trampoline);

        (*proc->threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    void enqueue_new(thread_t *thread)
    {
        preempt_disable();
        std::size_t target;
        if (should_start.load(std::memory_order_relaxed))
            target = find_least_loaded(thread);
        else
            target = cpu::self().unsafe_get().idx;
        lib::bug_on(!can_run_on(thread, target));
        enqueue_on(thread, target, true);
        preempt_enable();
    }

    thread_t *spawn(std::uintptr_t ip, std::uintptr_t arg, nice_t nice)
    {
        auto thread = create_kthread(ip, arg, nice);
        enqueue_new(thread);
        return thread;
    }

    process_t *get_process(pid_t pid)
    {
        if (pid < 0)
            return nullptr;

        auto locked = processes.lock();
        auto it = locked->find(pid);
        if (it == locked->end())
            return nullptr;
        return it->second;
    }

    bool wake_up(thread_t *thread, bool preempt)
    {
        if (thread->state != thread_state::sleeping &&
            thread->state != thread_state::blocked &&
            thread->state != thread_state::stopped)
            return false;

        preempt_disable();
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            thread->state = thread_state::runnable;

            rq.adjust(thread, false);
            rq.enqueue(thread);
            rq.nr_running++;

            if (preempt && rq.check_preempt_wakeup(thread))
                rq.current->flags |= thread_flags::needs_resched;
        }
        if (preempt_enable() && preempt)
            schedule();

        return true;
    }

    void sleep()
    {
        auto thread = current_thread();
        thread->state = thread_state::sleeping;
        schedule();
    }

    void block()
    {
        auto thread = current_thread();
        thread->state = thread_state::blocked;
        schedule();
    }

    void arm_thread_timeout(sleep_entry_t *entry, std::uint64_t ns)
    {
        const auto timer = chrono::main_timer();
        entry->deadline_ns = timer->ns() + ns;
        entry->expired = false;

        auto locked = sleep_list.lock();
        locked->insert(entry);
    }

    bool cancel_thread_timeout(sleep_entry_t *entry)
    {
        auto locked = sleep_list.lock();
        if (entry->expired)
            return false;

        locked->remove(entry);
        return true;
    }

    std::uint64_t sleep_for_ns(std::uint64_t ns)
    {
        if (ns == 0)
            return 0;

        const auto timer = chrono::main_timer();
        const auto deadline = timer->ns() + ns;

        auto thread = current_thread();
        sleep_entry_t entry {
            .thread = thread,
            .deadline_ns = deadline,
            .expired = false,
            .hook = { }
        };

        {
            auto locked = sleep_list.lock();
            locked->insert(&entry);
            thread->state = thread_state::sleeping;
        }

        schedule();

        if (!entry.expired)
        {
            auto locked = sleep_list.lock();
            locked->remove(&entry);
        }

        const auto now = timer->ns();
        return now >= deadline ? 0 : deadline - now;
    }

    bool yield()
    {
        preempt_disable();
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            auto curr = rq.current;
            lib::bug_on(curr != current_thread());

            if (!rq.queue.empty())
                curr->vruntime = std::max(curr->vruntime, rq.queue.last()->vruntime);
            curr->flags |= thread_flags::needs_resched;
        }
        preempt_enable();
        schedule();

        auto thread = current_thread();
        const bool interrupted = (thread->flags & thread_flags::interrupted) != thread_flags::none;
        thread->flags &= ~thread_flags::interrupted;
        return interrupted;
    }

    [[noreturn]] void thread_exit(int exit_code)
    {
        preempt_disable();

        auto thread = current_thread();
        auto proc = thread->proc;

        thread->exit_code = exit_code;
        thread->state = thread_state::zombie;

        lib::bug_on(!proc->threads.lock()->erase(thread->tid));

        if (proc->alive_threads.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            lib::panic_if(proc->pid == 0, "attempted to kill kernel process");
            lib::panic_if(proc->pid == 1, "attempted to kill init");

            proc->exit_code = exit_code;
            proc->is_zombie = true;

            auto init = get_process(1);
            lib::bug_on(!init);

            {
                auto locked = proc->children.lock();
                for (auto &[pid, child] : *locked)
                {
                    child->parent = init;
                    (*init->children.lock())[pid] = child;
                }
                locked->clear();
            }

            if (proc->parent)
            {
                // TODO: send SIGCHLD to parent and wake up waitpid
            }
        }

        lib::debug("thread [{}:{}] exited with code {}", proc->pid, thread->tid, exit_code);

        preempt_enable();
        schedule();
        std::unreachable();
    }

    [[noreturn]] void process_exit(int exit_code)
    {
        preempt_disable();

        auto current = current_thread();
        auto proc = current->proc;

        {
            auto locked = proc->threads.lock();
            for (auto &[tid, thread] : *locked)
            {
                if (thread == current)
                    continue;

                switch (thread->state)
                {
                    case thread_state::sleeping:
                    case thread_state::blocked:
                    case thread_state::stopped:
                        // TODO: remove from wait queue
                        [[fallthrough]];
                    case thread_state::running:
                    case thread_state::runnable:
                        thread->exit_code = exit_code;
                        thread->state = thread_state::dead;
                        break;
                    case thread_state::zombie:
                    case thread_state::dead:
                        break;
                }

                proc->alive_threads.fetch_sub(1, std::memory_order_acq_rel);
            }
        }

        thread_exit(exit_code);
        std::unreachable();
    }

    void tick()
    {
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            auto curr = rq.current;
            if (!curr->is_idle())
            {
                const auto timer = chrono::main_timer();
                rq.update_current(timer->ns());
            }
            curr->flags |= thread_flags::needs_resched;

            if (is_preempt_disabled())
            {
                const auto timeslice = rq.calc_timeslice(rq.current->weight);
                arch::arm_timer_ns(timeslice);
            }
        }

        {
            const auto timer = chrono::main_timer();
            const auto now = timer->ns();

            auto locked = sleep_list.lock();
            auto it = locked->begin();
            while (it != locked->end())
            {
                auto entry = (it++).value();
                if (entry->deadline_ns > now)
                    break;

                locked->remove(entry);
                entry->expired = true;
                wake_up(entry->thread, false);
            }
        }

        load_balance();
    }

    void load_balance()
    {
        if (cpu::count() == 1)
            return;

        preempt_disable();
        auto &my_rq = run_queue.unsafe_get();

        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        if (now - my_rq.load_update < balance_interval_ns)
        {
            preempt_enable();
            return;
        }
        my_rq.load_update = now;

        auto busiest_cpu = my_rq.cpu_idx;
        std::uint64_t most_load = 0;
        std::size_t most_nr = 0;

        for (std::size_t i = 0; i < cpu::count(); i++)
        {
            if (i == my_rq.cpu_idx)
                continue;

            auto &rq = run_queue.unsafe_get(cpu::local::nth_base(i));

            const auto load = rq.total_weight;
            const auto nr = rq.nr_running;

            if (load > most_load)
            {
                most_load = load;
                most_nr = nr;
                busiest_cpu = i;
            }
        }

        const auto my_load = my_rq.total_weight;
        if (busiest_cpu == my_rq.cpu_idx || most_load < my_load || most_nr <= 1)
        {
            preempt_enable();
            return;
        }

        const auto imbalance = most_load - my_load;
        if (imbalance < most_load / 4)
        {
            preempt_enable();
            return;
        }

        auto &src_rq = run_queue.unsafe_get(cpu::local::nth_base(busiest_cpu));
        auto first = &my_rq;
        auto second = &src_rq;
        if (my_rq.cpu_idx > src_rq.cpu_idx)
            std::swap(first, second);

        first->lock.lock();
        second->lock.lock();

        if (src_rq.total_weight <= my_rq.total_weight || src_rq.nr_running <= 1)
        {
            second->lock.unlock();
            first->lock.unlock();
            preempt_enable();
            return;
        }

        const auto locked_imbalance = src_rq.total_weight - my_rq.total_weight;
        if (locked_imbalance < src_rq.total_weight / 4)
        {
            second->lock.unlock();
            first->lock.unlock();
            preempt_enable();
            return;
        }

        const auto target = locked_imbalance / 2;
        std::uint64_t migrated_weight = 0;
        std::size_t migrations = 0;

        auto *thread = src_rq.queue.last();
        while (thread && migrations < balance_max_nr && migrated_weight < target)
        {
            auto prev = thread->hook.predecessor;

            if (thread == src_rq.current || !can_run_on(thread, my_rq.cpu_idx) ||
                migrated_weight + thread->weight > target + target / 2)
            {
                thread = prev;
                continue;
            }

            src_rq.dequeue(thread);
            src_rq.nr_running--;

            const auto delta = static_cast<std::int64_t>(
                thread->vruntime - src_rq._min_vruntime
            );
            thread->vruntime = my_rq._min_vruntime + std::max(0l, delta);

            my_rq.enqueue(thread);
            my_rq.nr_running++;

            migrated_weight += thread->weight;
            migrations++;

            thread = prev;
        }

        second->lock.unlock();
        first->lock.unlock();

        preempt_enable();
    }

    void set_affinity(pid_t pid, lib::bitmap mask)
    {
        if (mask.empty())
            return;

        if (pid == 0)
        {
            auto thread = current_thread();
            thread->affinity = std::move(mask);

            if (!thread->affinity.get(thread->running_on->idx))
                thread->flags |= thread_flags::needs_resched;
            return;
        }

        auto proc = get_process(pid);
        if (!proc)
            return;

        auto locked = proc->threads.lock();
        if (locked->empty())
            return;

        for (auto &[tid, thread] : *locked)
        {
            thread->affinity = mask;
            if (thread->state == thread_state::running &&
                !thread->affinity.get(thread->running_on->idx))
                thread->flags |= thread_flags::needs_resched;
        }
    }

    lib::bitmap get_affinity(pid_t pid)
    {
        if (pid == 0)
            return current_thread()->affinity;

        auto proc = get_process(pid);
        if (!proc)
            return 0;

        auto locked = proc->threads.lock();
        if (locked->empty())
            return 0;

        // return first thread affinity
        return locked->begin()->second->affinity;
    }

    int setpgid(pid_t pid, pid_t pgid)
    {
        // TODO-SCHED-REWRITE
        return -1;
    }

    pid_t getpgid(pid_t pid)
    {
        // TODO-SCHED-REWRITE
        return -1;
    }

    pid_t setsid()
    {
        // TODO-SCHED-REWRITE
        return -1;
    }

    pid_t getsid(pid_t pid)
    {
        // TODO-SCHED-REWRITE
        return -1;
    }

    pid_t clone(const kclone_args &args)
    {
        // TODO
        return -1;
    }

    int exec(const vfs::path &path, std::vector<std::string> argv, std::vector<std::string> envp)
    {
        // TODO
        return -1;
    }

    pid_t waitpid(const wait_options_t &options, int *status)
    {
        // TODO
        return -1;
    }
} // namespace sched
