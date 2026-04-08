// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.chrono;

namespace sched
{
    namespace
    {
        cpu_local(run_queue_t, local_rq);

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
    } // namespace

    void init()
    {
        // TODO
        // create kernel process
    }

    [[noreturn]] void start()
    {
        // TODO
        // set up idle threads
        // start the scheduler
        std::unreachable();
    }

    thread_t *current_thread()
    {
        return local_rq.read<thread_t *, &run_queue_t::current>();
    }

    process_t *current_process()
    {
        return current_thread()->proc;
    }

    void schedule()
    {
        // TODO
    }

    thread_t *create_kthread(nice_t nice, std::uintptr_t ip, std::uintptr_t arg)
    {
        // TODO
        return nullptr;
    }

    thread_t *create_thread(process_t *proc, int nice, std::uintptr_t entry, std::uintptr_t stack)
    {
        // TODO
        return nullptr;
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
        // TODO
        return false;
    }

    void sleep()
    {
        // TODO
    }

    void block()
    {
        // TODO
    }

    std::uint64_t sleep_for_ns(std::uint64_t ns)
    {
        // TODO
        return ns;
    }

    void yield()
    {
        // TODO
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

        auto &rq = local_rq.unsafe_get();
        const std::unique_lock _ { rq.lock };

        auto curr = rq.current;
        if (curr == rq.idle)
            return;

        auto &cfs = rq.cfs;
        // TODO: don't include irq time
        cfs.update_current(now);

        const auto ideal = cfs.calc_timeslice(curr->entity.weight);
        const auto delta = curr->entity.total_runtime - curr->entity.prev_runtime;
        if (delta > ideal)
            curr->flags |= thread_flags::needs_resched;

        const auto left = cfs.queue.first();
        if (left && left != &curr->entity)
        {
            const auto diff = static_cast<std::int64_t>(curr->entity.vruntime - left->vruntime);
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

            auto &rq = local_rq.unsafe_get();
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
            auto &rq = local_rq.unsafe_get();
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
