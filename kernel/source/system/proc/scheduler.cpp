// Copyright (C) 2024-2026  ilobilo

module system.sched;

import drivers.fs.procfs;
import drivers.timers;
import system.bin.exec;
import system.chrono;
import system.cpu;
import system.vfs;
import arch;
import fmt;

namespace sched
{
    namespace arch
    {
        using namespace ::arch;
    } // namespace arch

    namespace
    {
        cpu_local(run_queue_t, run_queue);

        using dead_threads_t = lib::locker<
            lib::list<
                std::shared_ptr<thread_t>
            >, lib::spinlock
        >;

        cpu_local(dead_threads_t, dead_threads);
        cpu_local(wait_queue_t, dead_bell);
        cpu_local(bool, need_reaper_wake);

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                std::shared_ptr<process_t>
            >, mutex
        > processes;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                std::weak_ptr<thread_t>
            >, mutex
        > threads;

        std::shared_ptr<process_t> kernel_proc;

        void push_dead(std::shared_ptr<thread_t> thread)
        {
            dead_threads.unsafe_get().lock()->push_back(std::move(thread));
            need_reaper_wake.unsafe_get() = true;
        }

        void add_cputime(process_t *proc, const thread_t *thread)
        {
            proc->utime_ns.fetch_add(thread->utime_ns, std::memory_order_relaxed);
            proc->stime_ns.fetch_add(thread->stime_ns, std::memory_order_relaxed);
        }

        std::shared_ptr<thread_t> take_thread(thread_t *thread)
        {
            auto locked = thread->proc->threads.lock();
            auto it = locked->find(thread->tid);
            if (it == locked->end())
                return nullptr;
            auto ret = std::move(it->second);
            locked->erase(it);
            return ret;
        }

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                std::weak_ptr<group_t>
            >, mutex
        > groups;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                std::weak_ptr<session_t>
            >, mutex
        > sessions;

        std::atomic_bool should_start = false;

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
                const std::unique_lock _ { rq.lock };

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
                rq.current->set_flag(thread_flags::needs_resched);
                if (cpu_idx != self.idx)
                    arch::wake_up_other(cpu_idx);
            }
        }

        std::uintptr_t allocate_kstack()
        {
            return lib::allocz<std::uintptr_t>(kstack_size);
        }

        void deallocate_kstack(std::uintptr_t kstack)
        {
            lib::free(kstack);
        }

        lib::bitmap create_affinity()
        {
            const auto ncpus = cpu::count();
            lib::bitmap ret { ncpus };
            ret.clear(0xFF);
            const auto trailing = ncpus % 8;
            if (trailing != 0)
                ret.data()[ret.size_bytes() - 1] &= static_cast<std::uint8_t>((1u << trailing) - 1);
            return ret;
        }

        std::shared_ptr<group_t> get_group(pid_t pgid)
        {
            auto locked = groups.lock();
            auto it = locked->find(pgid);
            if (it != locked->end())
            {
                if (auto group = it->second.lock())
                    return group;
                else
                    locked->erase(it);
            }
            return nullptr;
        }

        // std::shared_ptr<session_t> get_session(pid_t sid)
        // {
        //     auto locked = sessions.lock();
        //     auto it = locked->find(sid);
        //     if (it != locked->end())
        //     {
        //         if (auto session = it->second.lock())
        //             return session;
        //         else
        //             locked->erase(it);
        //     }
        //     return nullptr;
        // }

        void request_kill_siblings(int exit_code)
        {
            auto current = current_thread();
            auto process = current->proc;

            std::vector<std::shared_ptr<thread_t>> targets;
            {
                auto locked = process->threads.lock();
                targets.reserve(locked->size());
                for (auto &[tid, thread] : *locked)
                {
                    if (thread.get() == current)
                        continue;
                    targets.push_back(thread);
                }
            }

            for (auto &thread : targets)
                request_kill(thread.get(), exit_code);
        }

        [[noreturn]] void reap()
        {
            constexpr std::size_t wait_timeout_ns = 1'000'000'000;
            auto &deads = dead_threads.unsafe_get();
            auto &bell = dead_bell.unsafe_get();
            while (true)
            {
                lib::list<std::shared_ptr<thread_t>> batch;
                while (true)
                {
                    preempt_disable();
                    std::size_t gen;
                    {
                        auto locked = deads.lock();
                        gen = bell.snapshot_gen();
                        if (!locked->empty())
                        {
                            batch = std::move(*locked);
                            preempt_enable();
                            break;
                        }
                    }
                    preempt_enable();
                    bell.wait_prepared(gen, wait_timeout_ns);
                }

                while (!batch.empty())
                {
                    auto ptr = std::move(batch.front());
                    batch.pop_front();

                    while (ptr->on_cpu.load(std::memory_order_acquire))
                        arch::pause();
                }
            }
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

    lib::initgraph::task pid0_task
    {
        "sched.pid0-create",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            arch::bsp_initialised_stage(),
            timers::initialised_stage()
        },
        lib::initgraph::entail {
            pid0_created_stage()
        },
        [] {
            auto proc = std::make_shared<process_t>();

            proc->pid = 0;
            proc->group = nullptr;
            proc->session = nullptr;

            proc->vmspace = std::make_shared<vmm::vmspace>(
                std::make_shared<vmm::pagemap>(
                    vmm::kernel_pagemap.get()
                )
            );

            proc->vfs = nullptr;
            proc->fdt = nullptr;
            proc->cred = cred_t::root();
            proc->rlimits = nullptr;
            proc->sigactions = nullptr;

            (*processes.lock())[proc->pid] = proc;
            kernel_proc = std::move(proc);
        }
    };

    [[noreturn]] void start()
    {
        auto &self = cpu::self().unsafe_get();
        auto &rq = run_queue.unsafe_get();

        rq.cpu_idx = self.idx;
        rq.load_update = (self.idx * balance_interval_ns) / cpu::count();

        lib::bug_on(!kernel_proc);
        rq.idle = std::make_shared<thread_t>();
        rq.idle->kstack_base = allocate_kstack();
        rq.idle->kstack_top = rq.idle->kstack_base + kstack_size;
        rq.idle->self = rq.idle.get();
        rq.idle->tid = alloc_id();
        rq.idle->proc = kernel_proc.get();
        rq.idle->set_flag(thread_flags::kernel);
        rq.idle->nice = default_nice;
        rq.idle->weight = nice_to_weight(rq.idle->nice);
        rq.idle->inv_weight = nice_to_inv_weight(rq.idle->nice);
        rq.idle->affinity = create_affinity();

        arch::init_thread(
            rq.idle.get(),
            reinterpret_cast<std::uintptr_t>(arch::halt),
            true, false, false
        );

        rq.idle->set_flag(thread_flags::idle);
        rq.idle->affinity.clear(0);
        rq.idle->affinity.set(rq.cpu_idx, true);

        rq.current = rq.idle.get();
        rq.current->running_on = self.self;
        rq.current->on_cpu.store(true, std::memory_order_relaxed);
        rq.current->state.store(thread_state::running, std::memory_order_relaxed);

        arch::init_core(rq.current);

        (*kernel_proc->threads.lock())[rq.idle->tid] = rq.idle;
        (*threads.lock())[rq.idle->tid] = std::weak_ptr<thread_t>(rq.idle);
        kernel_proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        {
            auto reaper = create_kthread(
                reinterpret_cast<std::uintptr_t>(reap), 0, nice_t::max
            );
            reaper->affinity.clear(0);
            reaper->affinity.set(rq.cpu_idx, true);
            enqueue_on(reaper.get(), rq.cpu_idx, true);
        }

        if (rq.cpu_idx == cpu::bsp_idx())
        {
            _running = true;
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

        thread_t *prev = rq.current;
        thread_t *next = nullptr;

        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        const auto delta = rq.update_current(now);
        if (!prev->is_idle())
            prev->stime_ns += delta;

        switch (prev->state.load(std::memory_order_relaxed))
        {
            case thread_state::running:
                prev->state.store(thread_state::runnable, std::memory_order_relaxed);
                if (prev->on_rq == nullptr && !prev->is_idle())
                    rq.enqueue(prev);
                break;
            case thread_state::runnable:
                break;
            case thread_state::sleeping:
            case thread_state::blocked:
            case thread_state::stopped:
                if (prev->on_rq != nullptr)
                    rq.dequeue(prev);
                if (!prev->is_idle())
                    rq.nr_running--;
                break;
            case thread_state::dead:
                if (prev->on_rq != nullptr)
                    rq.dequeue(prev);
                rq.current = nullptr;
                if (!prev->is_idle())
                    rq.nr_running--;

                if (!prev->saved_vmspace)
                    prev->saved_vmspace = prev->proc->vmspace;
                break;
        }

        prev->clear_flag(thread_flags::needs_resched);

        const auto pick_alive = [&] -> thread_t * {
            while (auto cand = rq.pick_next())
            {
                if (cand->state.load(std::memory_order_relaxed) != thread_state::dead)
                {
                    rq.dequeue(cand);
                    return cand;
                }
                rq.dequeue(cand);
                if (!cand->is_idle())
                    rq.nr_running--;
                if (!cand->saved_vmspace)
                    cand->saved_vmspace = cand->proc->vmspace;
            }
            return nullptr;
        };

        next = pick_alive();
        if (next == nullptr)
        {
            rq.lock.unlock();
            load_balance();
            rq.lock.lock();
            next = pick_alive();
        }

        if (next == nullptr)
            next = rq.idle.get();

        lib::bug_on(!can_run_on(next, rq.cpu_idx));

        next->state.store(thread_state::running, std::memory_order_relaxed);
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
            if (need_reaper_wake.unsafe_get())
            {
                need_reaper_wake.unsafe_get() = false;
                dead_bell.unsafe_get().wake_all();
            }
            return;
        }
        else
        {
            const auto &prev_vmspace = prev->saved_vmspace
                ? prev->saved_vmspace : prev->proc->vmspace;
            if (next->proc->vmspace != prev_vmspace)
            {
                if (prev_vmspace)
                    prev_vmspace->pmap->unload();
                next->proc->vmspace->pmap->load();
            }
        }

        if (self.in_interrupt.load(std::memory_order_relaxed))
            next->was_in_interrupt = &self.in_interrupt;
        next->needs_unlock = &rq.lock;
        next->prev_to_release = prev;
        next->on_cpu.store(true, std::memory_order_release);

        arch::arm_timer_ns(timeslice);
        arch::context_switch(prev, next);

        auto thread = current_thread();
        if (auto released = thread->prev_to_release)
        {
            thread->prev_to_release = nullptr;
            released->on_cpu.store(false, std::memory_order_release);
        }
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

        preempt_disable();
        if (need_reaper_wake.unsafe_get())
        {
            need_reaper_wake.unsafe_get() = false;
            dead_bell.unsafe_get().wake_all();
        }
        preempt_enable();
    }

    std::shared_ptr<process_t> create_process(const std::shared_ptr<process_t> &parent)
    {
        auto proc = std::make_shared<process_t>();

        if (!parent)
        {
            proc->pid = 1;
            proc->parent = proc;                                  // init parents itself

            proc->session = std::make_shared<session_t>();
            proc->session->sid = proc->pid;

            proc->group = std::make_shared<group_t>();
            proc->group->pgid = proc->pid;
            proc->group->session = proc->session;

            (*proc->group->members.lock())[proc->pid] = proc;
            (*proc->session->members.lock())[proc->group->pgid] = proc->group;

            (*groups.lock())[proc->group->pgid] = proc->group;
            (*sessions.lock())[proc->session->sid] = proc->session;
        }
        else
        {
            proc->pid = alloc_id();
            proc->parent = parent;

            proc->session = parent->session;
            proc->group = parent->group;

            (*proc->group->members.lock())[proc->pid] = proc;
        }

        lib::bug_on(get_process(proc->pid) != nullptr);
        (*processes.lock())[proc->pid] = proc;

        // the caller will set up the rest of the fields
        return proc;
    }

    std::shared_ptr<thread_t> create_kthread(std::uintptr_t ip, std::uintptr_t arg, nice_t nice)
    {
        auto proc = get_process(0);
        lib::bug_on(!proc);

        auto thread = std::make_shared<thread_t>();

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        thread->self = thread.get();

        thread->tid = alloc_id();
        thread->proc = proc.get();
        thread->set_flag(thread_flags::kernel);

        thread->nice = nice;
        thread->weight = nice_to_weight(thread->nice);
        thread->inv_weight = nice_to_inv_weight(thread->nice);

        thread->affinity = create_affinity();

        arch::init_thread(thread.get(), ip, arg, false, false);

        (*proc->threads.lock())[thread->tid] = thread;
        (*threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    std::shared_ptr<thread_t> create_uthread(
        const std::shared_ptr<process_t> &proc, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone,
        std::uintptr_t stack, nice_t nice
    )
    {
        lib::bug_on(!proc);
        const std::unique_lock _ { proc->lock };

        auto thread = std::make_shared<thread_t>();

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        thread->self = thread.get();

        if (auto locked = proc->threads.lock(); locked->empty())
            thread->tid = proc->pid;
        else
            thread->tid = alloc_id();

        thread->proc = proc.get();

        thread->nice = nice;
        thread->weight = nice_to_weight(thread->nice);
        thread->inv_weight = nice_to_inv_weight(thread->nice);

        if (stack == 0)
        {
            const auto prot = vmm::read | vmm::write;
            const auto flags = vmm::private_ | vmm::anonymous | vmm::stack;

            const auto ret = proc->vmspace->map(
                0, ustack_size,
                prot, prot, flags, nullptr, 0
            );

            if (!ret.has_value())
            {
                lib::error(
                    "sched: could not map user stack: {}",
                    lib::error_name(ret.error())
                );
                return nullptr;
            }

            thread->ustack_base = *ret;
            thread->ustack_top = thread->ustack_base + ustack_size;

            // TODO: guard page
        }
        else
        {
            thread->ustack_top = stack;
            thread->ustack_base = 0;
        }

        thread->affinity = create_affinity();

        arch::init_thread(thread.get(), ip, arg, is_trampoline, is_clone);

        (*proc->threads.lock())[thread->tid] = thread;
        (*threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    thread_t::~thread_t()
    {
        deallocate_kstack(kstack_base);

        if (ustack_base != 0 && saved_vmspace)
        {
            if (const auto ret = saved_vmspace->unmap(ustack_base, ustack_size); !ret)
            {
                lib::error(
                    "sched: could not unmap user stack: {}",
                    lib::error_name(ret.error())
                );
            }
        }

        arch::deinit_thread(this);
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

    std::shared_ptr<thread_t> spawn(std::uintptr_t ip, std::uintptr_t arg, nice_t nice)
    {
        auto thread = create_kthread(ip, arg, nice);
        enqueue_new(thread.get());
        return thread;
    }

    std::shared_ptr<process_t> get_process(pid_t pid)
    {
        if (pid < 0)
            return nullptr;

        auto locked = processes.lock();
        auto it = locked->find(pid);
        if (it == locked->end())
            return nullptr;
        return it->second;
    }

    std::shared_ptr<thread_t> get_thread(pid_t tid)
    {
        if (tid < 0)
            return nullptr;

        auto locked = threads.lock();
        auto it = locked->find(tid);
        if (it == locked->end())
            return nullptr;
        auto ptr = it->second.lock();
        if (!ptr)
            locked->erase(it);
        return ptr;
    }

    std::size_t process_count()
    {
        return processes.lock()->size();
    }

    void for_each_process(std::function_ref<bool (const std::shared_ptr<process_t> &)> func)
    {
        std::vector<std::shared_ptr<process_t>> snapshot;
        {
            auto locked = processes.lock();
            snapshot.reserve(locked->size());
            for (const auto &[_, proc] : *locked)
                snapshot.push_back(proc);
        }
        for (auto &proc : snapshot)
        {
            if (!func(proc))
                break;
        }
    }

    bool wake_up(thread_t *thread, bool preempt, bool force)
    {
        auto state = thread->state.load(std::memory_order_acquire);
        while (true)
        {
            if (state == thread_state::stopped && !force)
            {
                thread->prev_state.store(thread_state::runnable, std::memory_order_relaxed);
                return false;
            }
            else if (state != thread_state::sleeping && state != thread_state::blocked)
                return false;

            if (thread->state.compare_exchange_weak(
                state, thread_state::runnable,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
                break;
        }

        preempt_disable();

        while (thread->on_cpu.load(std::memory_order_acquire))
            arch::pause();

        const auto &self = cpu::self().unsafe_get();
        const auto self_idx = self.idx;
        std::size_t target;
        if (should_start.load(std::memory_order_relaxed))
            target = find_least_loaded(thread);
        else
            target = self_idx;

        enqueue_on(thread, target, false);

        const bool on_self = (target == self_idx);
        const bool in_interrupt = self.in_interrupt.load(std::memory_order_relaxed);
        if (preempt_enable() && preempt && on_self)
        {
            if (in_interrupt)
                current_thread()->set_flag(thread_flags::needs_resched);
            else
                schedule();
        }

        return true;
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
            curr->set_flag(thread_flags::needs_resched);
        }
        preempt_enable();
        schedule();

        auto thread = current_thread();
        return thread->test_and_clear_flag(thread_flags::interrupted);
    }

    void request_kill(thread_t *thread, int exit_code)
    {
        thread->pending_exit_code.store(exit_code, std::memory_order_relaxed);

        if (thread->has_flag(thread_flags::kill_pending))
            return;
        thread->set_flag(thread_flags::kill_pending);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        switch (thread->state.load(std::memory_order_acquire))
        {
            case thread_state::sleeping:
                thread->set_flag(thread_flags::interrupted);
                [[fallthrough]];
            case thread_state::blocked:
                if (auto wq = thread->on_wait_queue.load(std::memory_order_acquire))
                    wq->unlink_atomic(thread->on_wait_queue, thread->wait_entry);
                wake_up(thread, true);
                break;
            case thread_state::stopped:
                wake_up(thread, true, true);
                break;
            case thread_state::running:
                thread->set_flag(thread_flags::needs_resched);
                if (auto on = thread->running_on; on && on != &cpu::self().unsafe_get())
                    arch::wake_up_other(on->idx);
                break;
            case thread_state::runnable:
            case thread_state::dead:
                break;
        }
    }

    void die_if_kill_pending()
    {
        auto thread = current_thread();
        if (thread->has_flag(thread_flags::kill_pending))
            thread_exit(thread->pending_exit_code.load(std::memory_order_relaxed));
    }

    [[noreturn]] void thread_exit(int exit_code)
    {
        preempt_disable();

        auto thread = current_thread();
        auto proc = thread->proc;

        if (!thread->saved_vmspace)
            thread->saved_vmspace = proc->vmspace;

        futex::cleanup_robust_list(thread);

        if (thread->clear_child_tid)
        {
            const pid_t zero = 0;
            const auto clear_child_tid = reinterpret_cast<pid_t __user *>(thread->clear_child_tid);
            if (!lib::copy_to_user(clear_child_tid, &zero, sizeof(pid_t)))
                lib::error("sched: failed to write to clear_child_tid");

            const auto uaddr = reinterpret_cast<std::uint32_t __user *>(thread->clear_child_tid);
            if (auto key = futex::resolve(uaddr, true))
                futex::wake(*key, 1, futex::bitset_match_any);
        }

        auto self_ptr = take_thread(thread);
        threads.lock()->erase(thread->tid);

        add_cputime(proc, thread);

        if (proc->alive_threads.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            lib::panic_if(proc->pid == 0, "attempted to kill kernel process");
            lib::panic_if(proc->pid == 1, "attempted to kill init");

            cancel_alarm(&proc->alarm);

            auto init = get_process(1);
            lib::bug_on(!init);

            {
                auto locked = proc->children.lock();
                auto init_children = init->children.lock();
                for (auto &[pid, child] : *locked)
                {
                    child->parent = init;
                    const bool was_zombie = child->is_zombie;
                    init_children->emplace(pid, std::move(child));
                    if (was_zombie)
                        init->wait_child.wake_one();
                }
                locked->clear();
            }

            {
                if (proc->pid == proc->session->sid)
                {
                    std::shared_ptr<ctty_base> ctty;
                    {
                        auto locked = proc->session->ctty.lock();
                        ctty = locked.value();
                    }
                    if (ctty)
                        ctty->detach(proc->session.get());
                }

                auto glocked = proc->group->members.lock();
                glocked->erase(proc->pid);
                if (glocked->empty())
                {
                    auto slocked = proc->session->members.lock();
                    slocked->erase(proc->group->pgid);
                    groups.lock()->erase(proc->group->pgid);
                }
            }

            proc->fdt.reset();

            // clean up early
            proc->vfs.reset();
            proc->cred.reset();
            proc->rlimits.reset();

            proc->exit_code = exit_code;
            proc->is_zombie = true;

            if (auto parent = proc->parent.lock())
            {
                if (proc->vfork_pending)
                {
                    proc->vfork_pending = false;
                    parent->vfork_done.wake_all();
                }

                const auto chld_code = proc->killed_by_signal
                    ? (proc->dumped_core ? cld_dumped : cld_killed)
                    : cld_exited;
                const auto chld_status = proc->killed_by_signal ? proc->term_signal : exit_code;

                siginfo_t info {
                    .signo = sigchld,
                    .code = chld_code,
                    .err = 0,
                    .pid = proc->pid,
                    .uid = 0,
                    .status = chld_status,
                    .addr = 0,
                    .value = 0
                };
                send_signal(parent.get(), info);
                parent->wait_child.wake_one();
            }
        }

        thread->state.store(thread_state::dead, std::memory_order_release);
        if (self_ptr)
            push_dead(std::move(self_ptr));
        preempt_enable();
        schedule();
        std::unreachable();
    }

    [[noreturn]] void process_exit(int exit_code)
    {
        auto proc = current_process();
        proc->lock.lock();
        request_kill_siblings(exit_code);

        while (proc->alive_threads.load(std::memory_order_acquire) > 1)
            yield();

        proc->lock.unlock();
        thread_exit(exit_code);
    }

    [[noreturn]] void process_exit_signal(int signo, bool core_dumped)
    {
        auto proc = current_process();
        proc->killed_by_signal = true;
        proc->term_signal = signo;
        proc->dumped_core = core_dumped;
        process_exit(0);
    }

    void tick(bool from_user)
    {
        process_t *charge_proc = nullptr;
        std::uint64_t cpu_delta = 0;
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            if (auto curr = rq.current)
            {
                if (!curr->is_idle())
                {
                    const auto timer = chrono::main_timer();
                    const auto now = timer->ns();
                    const auto delta = rq.update_current(now);

                    if (from_user)
                        curr->utime_ns += delta;
                    else
                        curr->stime_ns += delta;

                    if (rq.tick_last_ns != 0 && now > rq.tick_last_ns)
                    {
                        cpu_delta = now - rq.tick_last_ns;
                        charge_proc = curr->proc;
                    }
                    rq.tick_last_ns = now;
                }
                curr->set_flag(thread_flags::needs_resched);

                if (is_preempt_disabled())
                {
                    const auto timeslice = rq.calc_timeslice(curr->weight);
                    arch::arm_timer_ns(timeslice);
                }
            }
        }

        if (charge_proc != nullptr && cpu_delta > 0)
            charge_cpu_itimers(charge_proc, cpu_delta, from_user);

        expire_timeouts();
        expire_alarms();
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

        auto thread = src_rq.queue.last();
        while (thread && migrations < balance_max_nr && migrated_weight < target)
        {
            auto prev = thread->hook.predecessor;

            if (thread == src_rq.current || !can_run_on(thread, my_rq.cpu_idx) ||
                thread->is_kernel() || // do not migrate kernel threads
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

    int set_priority(int which, int who, int prio)
    {
        auto proc = current_process();
        const auto cred = proc->cred;

        prio = std::clamp<int>(prio, nice_t::min, nice_t::max);
        const auto new_nice = static_cast<nice_t>(prio);

        auto ret = -ESRCH;
        const auto handle = [&](process_t *target) {
            const auto tcred = target->cred;

            const bool perm_ok = capable(cred, cap_t::sys_nice) ||
                (tcred && (cred->euid == tcred->ruid || cred->euid == tcred->euid));

            if (!perm_ok)
            {
                ret = -EPERM;
                return;
            }

            auto cur_min = nice_t::max;
            {
                auto locked = target->threads.lock();
                for (auto &[tid, thread] : *locked)
                    cur_min = std::min(cur_min, thread->nice.value());
            }

            if (new_nice.value() < cur_min)
            {
                rlimit nlim { 0, 0 };
                if (const auto rlimits = target->rlimits)
                    nlim = rlimits->get(rlimit_nice);
                const long min_allowed = 20l - static_cast<long>(nlim.cur);
                if (new_nice.value() < min_allowed && !capable(cred, cap_t::sys_nice))
                {
                    ret = -EACCES;
                    return;
                }
            }

            if (ret == -ESRCH)
                ret = 0;

            auto locked = target->threads.lock();
            for (auto &[tid, thrd] : *locked)
            {
                auto thread = thrd.get();
                run_queue_t *rq = static_cast<run_queue_t *>(thread->on_rq);
                if (!rq && thread->state.load(std::memory_order_acquire) == thread_state::running)
                {
                    if (const auto running_on = thread->running_on)
                        rq = &run_queue.unsafe_get(cpu::local::nth_base(running_on->idx));
                }

                if (rq)
                {
                    const std::unique_lock _ { rq->lock };
                    const bool on_queue = (thread->on_rq == rq);
                    if (on_queue)
                        rq->dequeue(thread);
                    thread->nice = new_nice;
                    thread->weight = nice_to_weight(new_nice);
                    thread->inv_weight = nice_to_inv_weight(new_nice);
                    if (on_queue)
                        rq->enqueue(thread);
                }
                else
                {
                    thread->nice = new_nice;
                    thread->weight = nice_to_weight(new_nice);
                    thread->inv_weight = nice_to_inv_weight(new_nice);
                }
            }
        };

        switch (which)
        {
            case prio_process:
                if (auto target = (who == 0) ? proc->shared_from_this() : get_process(who))
                    handle(target.get());
                break;
            case prio_pgrp:
            {
                auto group = get_group((who == 0) ? proc->group->pgid : static_cast<pid_t>(who));
                if (!group)
                    break;
                auto locked = group->members.lock();
                for (auto &[pid, weak] : *locked)
                {
                    if (auto target = weak.lock())
                        handle(target.get());
                }
                break;
            }
            case prio_user:
            {
                const uid_t uid = (who == 0) ? proc->cred->ruid : static_cast<uid_t>(who);
                auto locked = processes.lock();
                for (auto &[pid, target] : *locked)
                {
                    if (target->cred && target->cred->ruid == uid)
                        handle(target.get());
                }
                break;
            }
            default:
                return -EINVAL;
        }

        return ret;
    }

    int get_priority(int which, int who)
    {
        auto proc = current_process();

        auto ret = -ESRCH;
        const auto check_proc = [&](process_t *proc) {
            auto locked = proc->threads.lock();
            for (auto &[tid, t] : *locked)
            {
                const int niceval = 20 - t->nice.value();
                if (niceval > ret)
                    ret = niceval;
            }
        };

        switch (which)
        {
            case prio_process:
                if (auto target = (who == 0) ? proc->shared_from_this() : get_process(who))
                    check_proc(target.get());
                break;
            case prio_pgrp:
            {
                auto group = get_group((who == 0) ? proc->group->pgid : static_cast<pid_t>(who));
                if (!group)
                    break;
                auto locked = group->members.lock();
                for (auto &[pid, weak] : *locked)
                {
                    if (auto target = weak.lock())
                        check_proc(target.get());
                }
                break;
            }
            case prio_user:
            {
                const uid_t uid = (who == 0) ? proc->cred->ruid : static_cast<uid_t>(who);
                auto locked = processes.lock();
                for (auto &[pid, target] : *locked)
                {
                    if (target->cred && target->cred->ruid == uid)
                        check_proc(target.get());
                }
                break;
            }
            default:
                return -EINVAL;
        }
        return ret;
    }

    int group_t::signal_all(int sig)
    {
        if (sig < 0 || sig > static_cast<int>(nsig))
            return -EINVAL;

        const auto caller = current_process();
        siginfo_t info {
            .signo = sig,
            .code = si_user,
            .err = 0,
            .pid = caller->pid,
            .uid = caller->cred->ruid,
            .status = 0,
            .addr = 0,
            .value = 0,
        };

        std::vector<std::shared_ptr<process_t>> targets;
        {
            auto locked = members.lock();
            if (locked->empty())
                return -ESRCH;

            targets.reserve(locked->size());
            for (auto it = locked->begin(); it != locked->end(); )
            {
                if (auto ptr = it->second.lock())
                {
                    targets.push_back(std::move(ptr));
                    it++;
                }
                else it = locked->erase(it);
            }
        }

        bool any_perm = false;
        for (auto &proc : targets)
        {
            if (!check_kill(sig, proc.get()))
                continue;

            any_perm = true;
            if (sig == 0)
                continue;

            send_signal(proc.get(), info);
        }
        return any_perm ? 0 : -EPERM;
    }

    int setpgid(pid_t pid, pid_t pgid)
    {
        if (pgid < 0)
            return -EINVAL;

        const auto proc = current_process();
        if (pid == 0)
            pid = proc->pid;
        if (pgid == 0)
            pgid = pid;

        const auto target = get_process(pid);
        if (!target)
            return -ESRCH;

        if (target->group->pgid == pgid)
            return 0;

        // is leader
        if (pid == target->session->sid)
            return -EPERM;

        if (proc->children.lock()->contains(pid))
        {
            if (target->has_execved)
                return -EACCES;

            if (proc->session != target->session)
                return -EPERM;
        }
        else if (pid != proc->pid)
            return -ESRCH;

        auto target_group = get_group(pgid);
        if (!target_group)
        {
            if (pgid != pid)
                return -EPERM;

            const auto create_group = [&] {
                auto group = std::make_shared<group_t>();
                group->pgid = target->pid;
                group->session = target->session;
                return group;
            };

            auto glocked = groups.lock();
            auto it = glocked->find(pgid);

            if (it == glocked->end())
            {
                target_group = create_group();
                (*glocked)[pgid] = target_group;
                (*target->session->members.lock())[pgid] = target_group;
            }
            else
            {
                target_group = it->second.lock();
                if (!target_group)
                {
                    target_group = create_group();
                    it->second = target_group;
                    (*target->session->members.lock())[pgid] = target_group;
                }
            }
        }

        if (target_group->session != target->session)
            return -EPERM;

        const std::unique_lock _ { target->lock };
        if (target->group->pgid == pgid)
            return 0;
        {
            auto locked = target_group->members.lock();
            auto [it, inserted] = locked->emplace(target->pid, target);
            if (!inserted)
                return -EPERM;
        }
        {
            auto glocked = target->group->members.lock();
            lib::bug_on(glocked->erase(target->pid) == 0);
            if (glocked->empty())
            {
                auto slocked = target->session->members.lock();
                lib::bug_on(slocked->erase(target->group->pgid) == 0);
            }
        }

        target->group = std::move(target_group);
        return 0;
    }

    pid_t setsid()
    {
        const auto proc = current_process();
        if (proc->pid == proc->group->pgid)
            return -EPERM;

        auto session = std::make_shared<session_t>();
        session->sid = proc->pid;

        auto group = std::make_shared<group_t>();
        group->pgid = proc->pid;
        group->session = session;

        (*group->members.lock())[proc->pid] = proc->shared_from_this();
        (*session->members.lock())[group->pgid] = group;

        const std::unique_lock _ { proc->lock };
        {
            auto glocked = proc->group->members.lock();
            lib::bug_on(glocked->erase(proc->pid) == 0);
            if (glocked->empty())
            {
                auto slocked = proc->session->members.lock();
                lib::bug_on(slocked->erase(proc->group->pgid) == 0);
            }
        }

        (*groups.lock())[group->pgid] = group;
        (*sessions.lock())[session->sid] = session;

        proc->group = std::move(group);
        proc->session = std::move(session);
        return proc->session->sid;
    }

    pid_t clone(const kclone_args_t &args)
    {
        const auto caller_thread = current_thread();
        const auto caller_proc = caller_thread->proc;

        const auto flags = args.flags;

        if ((flags & (clone_child_settid | clone_child_cleartid)) && !args.child_tid)
            return -EFAULT;

        if ((flags & clone_parent_settid) && !args.parent_tid)
            return -EFAULT;

        if ((flags & clone_clear_sighand) && (flags & clone_sighand))
            return -EINVAL;

        if ((flags & clone_thread) && !(flags & clone_sighand))
            return -EINVAL;

        if ((flags & (clone_thread | clone_parent)) && args.exit_signal)
            return -EINVAL;

        if ((flags & clone_thread) && (flags & clone_pidfd))
            return -EINVAL;

        if (!(flags & clone_vm) && (flags & clone_sighand))
            return -EINVAL;

        if ((flags & clone_fs) && (flags & clone_newns))
            return -EINVAL;

        if ((flags & clone_fs) && (flags & clone_newuser))
            return -EINVAL;

        if ((flags & clone_newipc) && (flags & clone_sysvsem))
            return -EINVAL;

        if ((flags & clone_newpid) && (flags & (clone_thread | clone_parent)))
            return -EINVAL;

        if ((flags & clone_newuser) && (flags & clone_thread))
            return -EINVAL;

        // TODO: namespaces
        if ((flags & (clone_newipc | clone_newnet | clone_newpid | clone_newuser | clone_newuts)))
            return -EINVAL;

        if ((flags & clone_parent) && caller_proc->pid == 1)
            return -EINVAL;

        const auto needs_priv = flags & (clone_newcgroup | clone_newipc | clone_newnet |
            clone_newns | clone_newpid | clone_newuts | clone_newtime);
        if (needs_priv && !capable(caller_proc->cred, cap_t::sys_admin))
            return -EPERM;

        constexpr std::uint64_t stack_alignment = 16;
        std::uintptr_t stack_top = 0;
        if (args.stack != 0)
        {
            const auto size = static_cast<std::uintptr_t>(args.stack_size);

            constexpr auto min = vmm::vmspace::mmap_min;
            constexpr auto max = vmm::vmspace::vspace_top;
            if (args.stack > max || args.stack < min || size > args.stack || args.stack - size < min)
                return -EINVAL;

            stack_top = args.stack + size;
            if (stack_top & (stack_alignment - 1))
                return -EINVAL;
        }

        std::shared_ptr<process_t> target_proc;
        std::shared_ptr<thread_t> target_thread;
        const bool created_proc = !(flags & clone_thread);

        auto cleanup = [&] {
            if (target_thread)
            {
                if (!target_thread->saved_vmspace && target_proc)
                    target_thread->saved_vmspace = target_proc->vmspace;

                const auto tid = target_thread->tid;
                const bool freed_pid = created_proc && target_proc && tid == target_proc->pid;

                if (target_proc)
                {
                    target_proc->threads.lock()->erase(tid);
                    threads.lock()->erase(tid);
                    if (!created_proc)
                        target_proc->alive_threads.fetch_sub(1, std::memory_order_acq_rel);
                }
                target_thread.reset();
                if (!freed_pid)
                    free_id(tid);
            }
            if (target_proc && created_proc)
            {
                processes.lock()->erase(target_proc->pid);
                if (target_proc->group)
                    target_proc->group->members.lock()->erase(target_proc->pid);

                const auto pid = target_proc->pid;
                target_proc.reset();
                free_id(pid);
            }
        };

        if (created_proc)
        {
            auto parent_shared = (flags & clone_parent)
                ? caller_proc->parent.lock()
                : caller_proc->shared_from_this();
            target_proc = create_process(parent_shared);

            target_proc->no_new_privs = caller_proc->no_new_privs;

            if (flags & clone_vm)
            {
                // TODO: clear alternate signal stacks
                if (!(flags & clone_vfork)) { }

                target_proc->vmspace = caller_proc->vmspace;
            }
            else
            {
                auto pmap = std::make_shared<vmm::pagemap>();
                auto vmspace = caller_proc->vmspace->fork(std::move(pmap));
                target_proc->vmspace = std::move(vmspace);
            }

            if (flags & clone_fs)
                target_proc->vfs = caller_proc->vfs;
            else
                target_proc->vfs = caller_proc->vfs->clone();

            if (flags & clone_files)
                target_proc->fdt = caller_proc->fdt;
            else
                target_proc->fdt = caller_proc->fdt->clone();

            target_proc->cred = caller_proc->cred;
            target_proc->rlimits = caller_proc->rlimits->clone();
            target_proc->dumpable.store(
                caller_proc->dumpable.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );

            target_proc->pathname = caller_proc->pathname;
            target_proc->argv = caller_proc->argv;
        }
        else target_proc = caller_proc->shared_from_this();

        target_thread = create_uthread(
            target_proc, 0, 0, false, true,
            stack_top, caller_thread->nice
        );

        target_thread->sigmask = caller_thread->sigmask;

        // TODO: set_tid array

        if (flags & clone_child_settid)
        {
            target_thread->set_child_tid = reinterpret_cast<std::uintptr_t>(args.child_tid);
            const auto space = lib::classify_address(target_thread->set_child_tid, sizeof(pid_t));
            if (space != lib::address_space::user)
            {
                cleanup();
                return -EFAULT;
            }
        }

        if (flags & clone_child_cleartid)
            target_thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(args.child_tid);

        if (flags & clone_parent_settid)
        {
            if (!lib::copy_to_user(args.parent_tid, &target_thread->tid, sizeof(pid_t)))
            {
                cleanup();
                return -EFAULT;
            }
        }

        if (flags & clone_clear_sighand)
            target_proc->sigactions = std::make_shared<signal_actions_t>();
        else if (flags & clone_sighand)
            target_proc->sigactions = caller_proc->sigactions;
        else
            target_proc->sigactions = caller_proc->sigactions->clone();

        // TODO: cgroups
        // TODO: namespaces
        // TODO: pidfd

        // TODO: better abstraction
#if defined(__x86_64__)
        auto regs = reinterpret_cast<cpu::registers *>(
            target_thread->kstack_top - sizeof(cpu::registers)
        );
        std::memcpy(regs, caller_thread->saved_regs, sizeof(cpu::registers));

        regs->rax = 0;
        if (stack_top != 0)
            regs->rsp = stack_top;

        target_thread->ctx.rsp = reinterpret_cast<std::uintptr_t>(regs);

        if (flags & clone_settls)
            target_thread->adata.fs_base = args.tls;
        else
            target_thread->adata.fs_base = cpu::fs::read();
        target_thread->adata.gs_base = cpu::gs::read_kernel();
#endif

        if (!(flags & clone_thread))
        {
            if (auto parent = target_proc->parent.lock())
                (*parent->children.lock())[target_proc->pid] = target_proc;
        }

        if (flags & clone_vfork)
            target_proc->vfork_pending = true;

        sched::enqueue_new(target_thread.get());

        const auto target_tid = target_thread->tid;
        target_thread.reset();
        target_proc.reset();

        if (flags & clone_vfork)
            caller_proc->vfork_done.wait_killable();

        return target_tid;
    }

    int exec(
        const vfs::path &path, std::vector<std::string> argv,
        std::vector<std::string> envp, std::string pathname
    )
    {
        {
            auto old_thread = current_thread();
            auto process = old_thread->proc;

            const auto mount_flags = path.mnt ? path.mnt->flags : 0ul;
            if (mount_flags & vfs::ms_noexec)
                return -EACCES;

            const auto xattr_caps = vfs::getxattr(path, vfs::xattr_caps_name);
            if (has_secbit(process->cred->securebits, secbit_t::exec_restrict_file) &&
                process->cred->euid != 0 && !xattr_caps)
                return -EACCES;

            // TODO
            // if (has_secbit(cred->securebits, secbit_t::exec_deny_interactive) && opened file is interactive?)
            //     return -EACCES;

            auto &inode = path.dentry->inode;
            {
                const std::unique_lock _ { inode->lock };

                if (!vfs::check_access(path, process->cred, static_cast<unsigned>(access_mode::exec)))
                    return -EACCES;
            }

            auto file = vfs::file::create(path, 0, 0);
            auto image = bin::exec::probe(std::move(file));
            if (!image)
                return -lib::map_error(image.error());

            lib::bug_on(!image.value());

            const std::unique_lock _ { process->lock };
            request_kill_siblings(0);

            while (process->alive_threads.load(std::memory_order_acquire) > 1)
                yield();

            {
                const std::unique_lock _ { inode->lock };

                const bool nosuid = (mount_flags & vfs::ms_nosuid) || process->no_new_privs;

                std::optional<vfs::file_caps> fcaps;
                if (xattr_caps && !nosuid)
                    fcaps = vfs::parse_file_caps(xattr_caps->span());

                if (nosuid)
                {
                    auto stat = inode->stat;
                    stat.st_mode &= ~(s_isuid | s_isgid);
                    apply_exec_caps(process, stat, fcaps);
                }
                else apply_exec_caps(process, inode->stat, fcaps);
            }

            auto new_pmap = std::make_shared<vmm::pagemap>();
            auto new_vmspace = std::make_shared<vmm::vmspace>(std::move(new_pmap));

            preempt_disable();

            auto old_ptr = take_thread(old_thread);
            threads.lock()->erase(old_thread->tid);
            process->alive_threads.fetch_sub(1, std::memory_order_acq_rel);

            auto old_vmspace = process->vmspace;
            process->vmspace = std::move(new_vmspace);

            auto saved_pathname = std::move(process->pathname);
            auto saved_argv = std::move(process->argv);
            process->pathname = pathname;
            process->argv = argv;

            auto new_thread = (*image)->load({
                .pathname = std::move(pathname),
                .argv = std::move(argv),
                .envp = std::move(envp),
                .proc = process,
            });

            if (!new_thread)
            {
                process->alive_threads.fetch_add(1, std::memory_order_relaxed);
                (*threads.lock())[old_thread->tid] =
                    ((*process->threads.lock())[old_thread->tid] = std::move(old_ptr));

                process->vmspace = std::move(old_vmspace);
                process->pathname = std::move(saved_pathname);
                process->argv = std::move(saved_argv);
                preempt_enable();
                return -ENOEXEC;
            }
            lib::bug_on(new_thread->tid != process->pid);

            add_cputime(process, old_thread);

            old_thread->state.store(thread_state::dead, std::memory_order_release);
            old_thread->saved_vmspace = std::move(old_vmspace);
            if (old_ptr)
                push_dead(std::move(old_ptr));

            if (process->fdt.use_count() > 1)
                process->fdt = process->fdt->clone();
            process->fdt->close_on_exec();

            if (process->sigactions.use_count() > 1)
                process->sigactions = process->sigactions->clone();
            {
                const std::unique_lock _ { process->sigactions->lock };
                for (auto &action : process->sigactions->actions)
                {
                    if (action.handler != sig_dfl && action.handler != sig_ign)
                        action = sigaction_t { };
                }
            }

            process->has_execved = true;

            if (auto parent = process->parent.lock(); parent && process->vfork_pending)
            {
                process->vfork_pending = false;
                parent->vfork_done.wake_all();
            }

            sched::enqueue_new(new_thread.get());
        }
        preempt_enable();

        schedule();
        std::unreachable();
    }

    cputime_t process_cputime(process_t *proc)
    {
        cputime_t ret {
            proc->utime_ns.load(std::memory_order_relaxed),
            proc->stime_ns.load(std::memory_order_relaxed),
        };

        const auto locked = proc->threads.lock();
        for (const auto &[tid, thread] : *locked)
        {
            ret.utime_ns += thread->utime_ns;
            ret.stime_ns += thread->stime_ns;
        }
        return ret;
    }

    cputime_t thread_cputime(thread_t *thread)
    {
        return { thread->utime_ns, thread->stime_ns };
    }

    cputime_t children_cputime(process_t *proc)
    {
        return {
            proc->children_utime_ns.load(std::memory_order_relaxed),
            proc->children_stime_ns.load(std::memory_order_relaxed),
        };
    }

    pid_t waitpid(pid_t wait_pid, int options, int *status, cputime_t *usage)
    {
        const auto proc = current_process();
        const bool no_hang = options & wnohang;

        const auto matches = [&](const process_t &child) {
            if (wait_pid > 0)
                return child.pid == wait_pid;
            if (wait_pid == -1)
                return true;
            if (wait_pid == 0)
                return child.group->pgid == proc->group->pgid;
            return child.group->pgid == -wait_pid;
        };

        const auto child_cputime = [](process_t *child) {
            const auto self = process_cputime(child);
            const auto reaped = children_cputime(child);
            return cputime_t {
                self.utime_ns + reaped.utime_ns,
                self.stime_ns + reaped.stime_ns,
            };
        };

        while (true)
        {
            {
                const auto gen = proc->wait_child.snapshot_gen();
                auto locked = proc->children.lock();
                if (locked->empty())
                    return -ECHILD;

                for (auto it = locked->begin(); it != locked->end(); it++)
                {
                    auto &child = it->second;
                    if (!matches(*child))
                        continue;

                    if (!child->is_zombie)
                        continue;

                    const auto cpid = child->pid;
                    const auto code = child->exit_code;
                    const bool killed = child->killed_by_signal;
                    const int term_sig = child->term_signal;
                    const bool core = child->dumped_core;

                    const auto ctime = child_cputime(child.get());
                    proc->children_utime_ns.fetch_add(ctime.utime_ns, std::memory_order_relaxed);
                    proc->children_stime_ns.fetch_add(ctime.stime_ns, std::memory_order_relaxed);
                    if (usage != nullptr)
                        *usage = ctime;

                    locked->erase(it);
                    processes.lock()->erase(cpid);
                    free_id(cpid);

                    if (status != nullptr)
                    {
                        if (killed)
                            *status = (term_sig & 0x7F) | (core ? 0x80 : 0);
                        else
                            *status = (code & 0xFF) << 8;
                    }

                    return cpid;
                }

                if (options & (wuntraced | wcontinued))
                {
                    for (auto &[cpid, child] : *locked)
                    {
                        if (!matches(*child))
                            continue;

                        const std::unique_lock _ { child->report_lock };

                        if ((options & wuntraced) && child->pending_stop_sig != 0)
                        {
                            const int sig = child->pending_stop_sig;
                            if (!(options & wnowait))
                                child->pending_stop_sig = 0;

                            if (status != nullptr)
                                *status = (sig << 8) | 0x7F;
                            if (usage != nullptr)
                                *usage = child_cputime(child.get());
                            return child->pid;
                        }

                        if ((options & wcontinued) && child->pending_continued)
                        {
                            if (!(options & wnowait))
                                child->pending_continued = false;

                            if (status != nullptr)
                                *status = 0xFFFF;

                            if (usage != nullptr)
                                *usage = child_cputime(child.get());
                            return child->pid;
                        }
                    }
                }

                bool any = false;
                for (auto &[pid, child] : *locked)
                {
                    if (matches(*child))
                    {
                        any = true;
                        break;
                    }
                }
                if (!any)
                    return -ECHILD;

                if (no_hang)
                    return 0;

                locked.unlock();
                const auto res = proc->wait_child.wait_prepared(gen);
                if (res.interrupted || res.killed)
                    return -EINTR;
            }
        }
    }

    int kill(pid_t pid, int sig)
    {
        using namespace sched;

        if (sig < 0 || sig > static_cast<int>(nsig))
            return -EINVAL;

        if (pid < -1)
        {
            auto group = get_group(-pid);
            if (!group)
                return -ESRCH;
            return group->signal_all(sig);
        }

        const auto caller = current_process();
        if (pid == 0)
            return caller->group->signal_all(sig);

        siginfo_t info {
            .signo = sig,
            .code = si_user,
            .err = 0,
            .pid = caller->pid,
            .uid = caller->cred->ruid,
            .status = 0,
            .addr = 0,
            .value = 0,
        };

        if (pid > 0)
        {
            const auto target = get_process(pid);
            if (!target)
                return -ESRCH;

            if (!check_kill(sig, target.get()))
                return -EPERM;

            if (sig == 0)
                return 0;

            return send_signal(target.get(), info) ? 0 : -ESRCH;
        }

        bool any_perm = false;
        for (auto &[pid, proc] : *processes.lock())
        {
            if (pid == 0 || pid == 1)
                continue;

            if (!check_kill(sig, proc.get()))
                continue;

            any_perm = true;
            if (sig == 0)
                continue;

            send_signal(proc.get(), info);
        }
        return any_perm ? 0 : -EPERM;
    }

    namespace
    {
        char state_letter(process_t *proc)
        {
            if (proc->is_zombie)
                return 'Z';

            bool any_runnable = false;
            bool any_stopped = false;

            auto locked = proc->threads.lock();
            for (const auto &[tid, thread] : *locked)
            {
                switch (thread->state.load(std::memory_order_relaxed))
                {
                    case thread_state::running:
                    case thread_state::runnable:
                        any_runnable = true;
                        break;
                    case thread_state::stopped:
                        any_stopped = true;
                        break;
                    default:
                        break;
                }
            }
            if (any_runnable)
                return 'R';
            if (any_stopped)
                return 'T';
            return 'S';
        }

        std::string proc_comm(process_t *proc)
        {
            if (!proc->comm.empty())
                return proc->comm;
            if (proc->pathname.empty())
                return fmt::format("pid_{}", proc->pid);
            return proc->pathname.basename();
        }

        lib::initgraph::task procfs_register_task
        {
            "sched.procfs.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { fs::procfs::registered_stage() },
            [] {
                using namespace fs::procfs;
                lib::bug_on(!register_per_pid("status",
                    make_file_ops([](process_t *proc) {
                        // TODO: VmPeak/VmSize/VmRSS/VmData/VmStk, SigQ, CapInh, etc.
                        const std::unique_lock _ { proc->lock };

                        auto parent_ptr = proc->parent.lock();
                        const pid_t ppid = parent_ptr ? parent_ptr->pid : 0;
                        const auto threads = proc->alive_threads.load(std::memory_order_relaxed);
                        const auto cred = proc->cred ? *proc->cred : cred_t { };

                        return fmt::format(
                            "Name:\t{}\n"
                            "State:\t{}\n"
                            "Tgid:\t{}\n"
                            "Pid:\t{}\n"
                            "PPid:\t{}\n"
                            "Uid:\t{} {} {} {}\n"
                            "Gid:\t{} {} {} {}\n"
                            "Threads:\t{}\n",
                            "NoNewPrivs: \t{}\n",
                            proc_comm(proc), state_letter(proc),
                            proc->pid, proc->pid, ppid,
                            cred.ruid, cred.euid, cred.suid, cred.fsuid,
                            cred.rgid, cred.egid, cred.sgid, cred.fsgid,
                            threads, proc->no_new_privs ? 1 : 0
                        );
                    }), node_type::file, 0444
                ));

                lib::bug_on(!register_per_pid("stat",
                    make_file_ops([](process_t *proc) {
                        // TODO: tty_nr, flags, faults, stime, starttime, rss, wchan, processor, policy
                        const std::unique_lock _ { proc->lock };

                        auto parent_ptr = proc->parent.lock();
                        const pid_t ppid = parent_ptr ? parent_ptr->pid : 0;
                        const pid_t pgid = proc->group ? proc->group->pgid : 0;
                        const pid_t sid = proc->session ? proc->session->sid : 0;
                        const auto state = state_letter(proc);
                        const auto comm = proc_comm(proc);
                        const auto threads = proc->alive_threads.load(std::memory_order_relaxed);

                        std::uint64_t vsize = 0;
                        std::uintptr_t startcode = 0;
                        std::uintptr_t endcode = 0;
                        std::uintptr_t startstack = 0;
                        std::uintptr_t startbrk = 0;
                        std::uintptr_t curr_brk = 0;

                        if (proc->vmspace)
                        {
                            const auto locked = proc->vmspace->tree.lock();
                            for (auto it = locked->begin(); it != locked->end(); it++)
                            {
                                const auto &entry = *it;
                                vsize += entry.endp - entry.startp;

                                if (entry.flags & vmm::stack)
                                    startstack = entry.startp;

                                if (entry.prot & vmm::exec)
                                {
                                    if (startcode == 0 || entry.startp < startcode)
                                        startcode = entry.startp;
                                    if (entry.endp > endcode)
                                        endcode = entry.endp;
                                }
                            }
                            startbrk = proc->vmspace->brk_start;
                            curr_brk = proc->vmspace->current_brk;
                        }

                        constexpr std::uint64_t hz = 100;
                        std::uint64_t total_runtime_ns = 0;
                        int nice_val = 0;
                        {
                            const auto locked = proc->threads.lock();
                            for (const auto &[tid, thread] : *locked)
                                total_runtime_ns += thread->total_runtime;
                            if (!locked->empty())
                                nice_val = locked->begin()->second->nice.value();
                        }
                        const auto utime_ticks = (total_runtime_ns / 1'000'000'000ul) * hz +
                            ((total_runtime_ns % 1'000'000'000ul) * hz) / 1'000'000'000ul;
                        const int priority = 20 - nice_val;

                        return fmt::format(
                            "{} ({}) {} {} {} {} 0 -1 0 0 0 0 0 "
                            "{} 0 0 0 "
                            "{} {} {} 0 "
                            "0 "
                            "{} 0 0 "
                            "{} {} "
                            "{} 0 0 "
                            "0 0 0 0 "
                            "0 0 0 "
                            "{} 0 0 0 "
                            "0 0 0 "
                            "0 0 {} {} "
                            "0 0 0 "
                            "{}\n",
                            proc->pid, comm, state, ppid, pgid, sid,
                            utime_ticks,
                            priority, nice_val, threads,
                            vsize,
                            startcode, endcode,
                            startstack,
                            proc->term_signal,
                            startbrk, curr_brk,
                            proc->exit_code
                        );
                    }), node_type::file, 0444
                ));

                lib::bug_on(!register_per_pid("comm",
                    make_file_ops(
                        [](process_t *proc) {
                            const std::unique_lock _ { proc->lock };
                            return proc_comm(proc) + '\n';
                        },
                        [](process_t *proc, std::string_view data) -> lib::expect<void> {
                            const std::unique_lock _ { proc->lock };
                            proc->comm = lib::trim(data.substr(0, 15));
                            return { };
                        }
                    ), node_type::file, 0644
                ));

                lib::bug_on(!register_per_pid("cmdline",
                    make_file_ops([](process_t *proc) {
                        const std::unique_lock _ { proc->lock };
                        std::string out;
                        for (const auto &arg : proc->argv)
                        {
                            out.append(arg);
                            out.push_back('\0');
                        }
                        return out;
                    }), node_type::file, 0444
                ));

                lib::bug_on(!register_per_pid("maps",
                    make_file_ops([](process_t *proc) {
                        // TODO: missing inode/dev/pathname columns and [stack]/[heap]/[vdso]
                        const std::unique_lock _ { proc->lock };
                        std::string out;
                        const auto vmspace = proc->vmspace;
                        if (!vmspace)
                            return out;

                        const auto locked = vmspace->tree.lock();
                        out.reserve(locked->size() * 64);
                        auto out_it = std::back_inserter(out);
                        for (auto it = locked->begin(); it != locked->end(); it++)
                        {
                            const auto &entry = *it;
                            const char r = (entry.prot & vmm::read)  ? 'r' : '-';
                            const char w = (entry.prot & vmm::write) ? 'w' : '-';
                            const char x = (entry.prot & vmm::exec)  ? 'x' : '-';
                            const char p = (entry.flags & vmm::shared) ? 's' : 'p';
                            fmt::format_to(out_it,
                                "{:016x}-{:016x} {}{}{}{} {:08x} 00:00 0\n",
                                entry.startp, entry.endp, r, w, x, p, entry.offp
                            );
                        }
                        return out;
                    }), node_type::file, 0400
                ));

                lib::bug_on(!register_per_pid("exe",
                    make_symlink_ops([](process_t *proc) {
                        const std::unique_lock _ { proc->lock };
                        // TODO
                        const auto res = vfs::resolve(proc->vfs->root, proc->pathname);
                        if (!res)
                            return proc->pathname.str();
                        return vfs::pathname_from(res->target);
                    }), node_type::symlink, 0777
                ));

                lib::bug_on(!register_per_pid("cwd",
                    make_symlink_ops([](process_t *proc) {
                        const std::unique_lock _ { proc->lock };
                        return vfs::pathname_from(proc->vfs->cwd);
                    }), node_type::symlink, 0777
                ));

                lib::bug_on(!register_per_pid("root",
                    make_symlink_ops([](process_t *proc) {
                        const std::unique_lock _ { proc->lock };
                        return vfs::pathname_from(proc->vfs->root);
                    }), node_type::symlink, 0777
                ));
            }
        };
    } // namespace
} // namespace sched
