// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.chrono;
import system.cpu;
import arch;

namespace sched
{
    namespace
    {
        cpu_local(run_queue_t, run_queue);

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, lib::spinlock
        > processes;

        std::atomic<pid_t> next_id { 1 };

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

        void migrate_thread(thread_t *thread, std::size_t target_cpu)
        {
            // TODO
        }

        std::uintptr_t allocate_kstack()
        {
            return lib::alloc<std::uintptr_t>(kstack_size);
        }

        lib::bitmap create_affinity()
        {
            lib::bitmap ret { cpu::count() };
            ret.clear(0xFF);
            return ret;
        }
    } // namespace

    void init()
    {
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
        proc->fds = nullptr;
        proc->cred = nullptr;
        proc->sigactions = nullptr;

        (*processes.lock())[proc->pid] = proc;
    }

    [[noreturn]] void start()
    {
        auto &rq = run_queue.unsafe_get();

        rq.cpu_idx = cpu::self().unsafe_get().idx;
        rq.total_weight = 0;
        rq._min_vruntime = 0;
        rq.nr_running = 0;
        rq.nr_switches = 0;

        rq.load_update = 0;
        rq.load_active = 0;

        rq.idle = create_kthread(reinterpret_cast<std::uintptr_t>(::arch::halt), true);
        rq.idle->flags |= thread_flags::idle;

        rq.current = rq.idle;
        rq.current->state = thread_state::running;

        arch::init_core(rq.current);

        schedule();
        std::unreachable();
    }

    void schedule()
    {
        preempt_disable();
        auto &rq = run_queue.unsafe_get();
        const std::unique_lock _ { rq.lock };

        auto prev = rq.current;
        thread_t *next = nullptr;

        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        rq.update_current(now);

        switch (prev->state)
        {
            case thread_state::running:
                prev->state = thread_state::runnable;
                if (!prev->in_rq)
                    rq.enqueue(prev);
                break;
            case thread_state::runnable:
                lib::panic("invalid thread state");
                std::unreachable();
            case thread_state::sleeping:
            case thread_state::blocked:
            case thread_state::stopped:
                if (prev->in_rq)
                    rq.dequeue(prev);
                rq.nr_running--;
                break;
            case thread_state::zombie:
            case thread_state::dead:
                if (prev->in_rq)
                    rq.dequeue(prev);
                rq.current = nullptr;
                rq.nr_running--;
                break;
        }

        prev->flags &= ~thread_flags::needs_resched;

        next = rq.pick_next();
        if (next == nullptr)
            next = rq.idle;
        else
            rq.dequeue(next);

        if (next == prev)
        {
            prev->state = thread_state::running;
            rq.current = prev;
        }

        next->state = thread_state::running;
        next->sched_time = now;
        next->prev_runtime = next->total_runtime;

        rq.current = next;
        rq.nr_switches++;

        if (next->proc != prev->proc)
            next->proc->vmspace->pmap->load();

        next->running_on = cpu::self().unsafe_get().self;

        preempt_enable();
        arch::context_switch(prev, next);
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

        arch::init_thread(thread, ip, arg, true);

        (*proc->threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    thread_t *create_uthread(process_t *proc, std::uintptr_t entry, std::uintptr_t stack, nice_t nice)
    {
        lib::bug_on(!proc);

        auto thread = new thread_t { };
        thread->self = thread;

        thread->tid = alloc_id();
        thread->proc = proc;

        thread->nice = nice;
        thread->weight = nice_to_weight(thread->nice);
        thread->inv_weight = nice_to_inv_weight(thread->nice);

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        thread->ustack_top = stack;
        thread->ustack_base = 0;

        thread->affinity = create_affinity();

        arch::init_thread(thread, entry, 0, false);

        (*proc->threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    void enqueue_new(thread_t *thread)
    {
        preempt_disable();
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            rq.adjust(thread, true);
            rq.enqueue(thread);
            rq.nr_running++;
        }
        preempt_enable();
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
        preempt_enable();

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
        thread->state = thread_state::sleeping;
        schedule();
    }

    std::uint64_t sleep_for_ns(std::uint64_t ns)
    {
        // TODO
        return ns;
    }

    void yield()
    {
        preempt_disable();
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            auto curr = rq.current;
            lib::bug_on(curr != current_thread());

            curr->vruntime = std::max(curr->vruntime, rq.queue.last()->vruntime);
            curr->flags |= thread_flags::needs_resched;
        }
        preempt_enable();
        schedule();
    }

    [[noreturn]] void thread_exit(int exit_code)
    {
        // TODO
        std::unreachable();
    }

    [[noreturn]] void process_exit(int exit_code)
    {
        // TODO
        std::unreachable();
    }

    void tick()
    {
        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        auto &rq = run_queue.unsafe_get();
        const std::unique_lock _ { rq.lock };

        auto curr = rq.current;
        if (curr == rq.idle)
            return;

        // TODO: don't include irq time
        rq.update_current(now);

        const auto ideal = rq.calc_timeslice(curr->weight);
        const auto delta = curr->total_runtime - curr->prev_runtime;
        if (delta > ideal)
            curr->flags |= thread_flags::needs_resched;

        const auto left = rq.queue.first();
        if (left && left != curr)
        {
            const auto diff = static_cast<std::int64_t>(curr->vruntime - left->vruntime);
            if (diff > static_cast<std::int64_t>(wakeup_gran_ns))
                curr->flags |= thread_flags::needs_resched;
        }
    }

    void load_balance()
    {
        // TODO
    }

    void set_affinity(pid_t pid, lib::bitmap mask)
    {
        if (mask.empty())
            return;

        if (pid == 0)
        {
            auto thread = current_thread();
            thread->affinity = std::move(mask);

            auto &rq = run_queue.unsafe_get();
            if (!thread->affinity.get(rq.cpu_idx))
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
            thread->affinity = std::move(mask);
            auto &rq = run_queue.unsafe_get();
            if (thread->state == thread_state::running && !thread->affinity.get(rq.cpu_idx))
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
        // TODO
        return -1;
    }

    pid_t getpgid(pid_t pid)
    {
        // TODO
        return -1;
    }

    pid_t setsid()
    {
        // TODO
        return -1;
    }

    pid_t getsid(pid_t pid)
    {
        // TODO
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
