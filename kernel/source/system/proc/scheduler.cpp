// Copyright (C) 2024-2026  ilobilo

module system.sched;

import drivers.timers;
import system.bin.exec;
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

        using dead_threads_t = lib::locker<
            lib::intrusive_list<
                thread_t,
                &thread_t::dead_hook
            >, lib::spinlock
        >;

        cpu_local(dead_threads_t, dead_threads);
        cpu_local(wait_queue_t, dead_bell);
        cpu_local(bool, need_reaper_wake);

        bool claim_for_reap(thread_t *thread)
        {
            if (thread->dead_listed.exchange(true, std::memory_order_acq_rel))
                return false;

            if (thread->has_flag(thread_flags::quiesce_pending))
                thread->proc->to_quiesce.fetch_sub(1, std::memory_order_acq_rel);

            dead_threads.unsafe_get().lock()->push_back(thread);
            need_reaper_wake.unsafe_get() = true;
            return true;
        }

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, mutex
        > processes;

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
            lib::bitmap ret { cpu::count() };
            ret.clear(0xFF);
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

        void kill_other_threads()
        {
            auto current = current_thread();
            auto process = current->proc;

            preempt_disable();
            auto locked = process->threads.lock();

            dead_threads_t::value_type to_kill;
            for (auto &[tid, thread] : *locked)
            {
                if (thread == current)
                    continue;
                to_kill.push_back(thread);
            }

            while (!to_kill.empty())
            {
                auto thread = to_kill.pop_back();
                lib::bug_on(locked->erase(thread->tid) != 1);
                process->alive_threads.fetch_sub(1, std::memory_order_acq_rel);

                if (!thread->saved_vmspace)
                    thread->saved_vmspace = process->vmspace;

                bool reap_directly = false;
                switch (thread->state.load(std::memory_order_relaxed))
                {
                    case thread_state::sleeping:
                        thread->set_flag(thread_flags::interrupted);
                        [[fallthrough]];
                    case thread_state::blocked:
                    case thread_state::stopped:
                    {
                        if (auto wq = thread->on_wait_queue.load(std::memory_order_acquire))
                            wq->unlink_atomic(thread->on_wait_queue, thread->wait_entry);
                        thread->state.store(thread_state::dead, std::memory_order_release);
                        reap_directly = true;
                        break;
                    }
                    case thread_state::running:
                        thread->set_flag(thread_flags::quiesce_pending | thread_flags::needs_resched);
                        process->to_quiesce.fetch_add(1, std::memory_order_acq_rel);
                        thread->state.store(thread_state::dead, std::memory_order_release);
                        break;
                    case thread_state::runnable:
                        if (auto rq = static_cast<run_queue_t *>(thread->on_rq))
                        {
                            const std::unique_lock _ { rq->lock };
                            if (thread->on_rq == rq)
                            {
                                rq->dequeue(thread);
                                if (!thread->is_idle())
                                    rq->nr_running--;
                            }
                        }
                        thread->state.store(thread_state::dead, std::memory_order_release);
                        reap_directly = true;
                        break;
                    case thread_state::dead:
                        break;
                }

                if (reap_directly)
                    claim_for_reap(thread);

                if (thread->running_on)
                    arch::wake_up_other(thread->running_on->idx);
            }
            preempt_enable();
        }

        [[noreturn]] void reap()
        {
            constexpr std::size_t wait_timeout_ns = 1'000'000'000;
            auto &deads = dead_threads.unsafe_get();
            while (true)
            {
                while (true)
                {
                    preempt_disable();
                    if (deads.lock()->empty())
                    {
                        preempt_enable();
                        dead_bell.unsafe_get().wait(wait_timeout_ns);
                        continue;
                    }
                    break;
                }

                auto copy = std::move(*deads.lock());
                preempt_enable();

                while (!copy.empty())
                {
                    auto thread = copy.pop_front();
                    delete thread;
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
            proc->cred = cred_t::root();
            proc->rlimits = nullptr;
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
        rq.idle->set_flag(thread_flags::idle);
        rq.idle->affinity.clear(0);
        rq.idle->affinity.set(rq.cpu_idx, true);

        rq.current = rq.idle;
        rq.current->running_on = self.self;
        rq.current->on_cpu.store(true, std::memory_order_relaxed);
        rq.current->state.store(thread_state::running, std::memory_order_relaxed);

        arch::init_core(rq.current);

        {
            auto reaper = create_kthread(
                reinterpret_cast<std::uintptr_t>(reap), 0, nice_t::max
            );
            reaper->affinity.clear(0);
            reaper->affinity.set(rq.cpu_idx, true);
            enqueue_on(reaper, rq.cpu_idx, true);
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

        rq.update_current(now);

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

                claim_for_reap(prev);
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

                claim_for_reap(cand);
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
            next = rq.idle;

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

    process_t *create_process(process_t *parent)
    {
        auto proc = new process_t { };

        if (parent == nullptr)
        {
            proc->pid = 1;
            proc->parent = proc;

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

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        thread->self = thread;

        thread->tid = alloc_id();
        thread->proc = proc;
        thread->set_flag(thread_flags::kernel);

        thread->nice = nice;
        thread->weight = nice_to_weight(thread->nice);
        thread->inv_weight = nice_to_inv_weight(thread->nice);

        thread->affinity = create_affinity();

        arch::init_thread(thread, ip, arg, false, false);

        (*proc->threads.lock())[thread->tid] = thread;
        proc->alive_threads.fetch_add(1, std::memory_order_relaxed);

        return thread;
    }

    thread_t *create_uthread(
        process_t *proc, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone,
        std::uintptr_t stack, nice_t nice
    )
    {
        lib::bug_on(!proc);
        const std::unique_lock _ { proc->lock };

        auto thread = new thread_t { };

        thread->kstack_base = allocate_kstack();
        thread->kstack_top = thread->kstack_base + kstack_size;

        thread->self = thread;

        if (auto locked = proc->threads.lock(); locked->empty())
            thread->tid = proc->pid;
        else
            thread->tid = alloc_id();

        thread->proc = proc;

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
                delete thread;
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

        arch::init_thread(thread, ip, arg, is_trampoline, is_clone);

        (*proc->threads.lock())[thread->tid] = thread;
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

    [[noreturn]] void thread_exit(int exit_code)
    {
        preempt_disable();

        auto thread = current_thread();
        auto proc = thread->proc;

        if (!thread->saved_vmspace)
            thread->saved_vmspace = proc->vmspace;

        thread->state.store(thread_state::dead, std::memory_order_release);

        if (thread->clear_child_tid)
        {
            const pid_t zero = 0;
            const auto clear_child_tid = reinterpret_cast<pid_t __user *>(thread->clear_child_tid);
            if (!lib::copy_to_user(clear_child_tid, &zero, sizeof(pid_t)))
                lib::error("sched: failed to write to clear_child_tid");
            // TODO: futex wake
        }

        proc->threads.lock()->erase(thread->tid);
        if (proc->alive_threads.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            lib::panic_if(proc->pid == 0, "attempted to kill kernel process");
            lib::panic_if(proc->pid == 1, "attempted to kill init");

            auto init = get_process(1);
            lib::bug_on(!init);

            {
                auto locked = proc->children.lock();
                for (auto &[pid, child] : *locked)
                {
                    child->parent = init;
                    (*init->children.lock())[pid] = child;
                    if (child->is_zombie)
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

            if (proc->parent && proc->vfork_pending)
            {
                proc->vfork_pending = false;
                proc->parent->vfork_done.wake_all();
            }

            if (proc->parent)
            {
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
                send_signal(proc->parent, info);
                proc->parent->wait_child.wake_one();
            }
        }

        preempt_enable();
        schedule();
        std::unreachable();
    }

    [[noreturn]] void process_exit(int exit_code)
    {
        auto proc = current_process();
        proc->lock.lock();
        kill_other_threads();

        while (proc->alive_threads.load(std::memory_order_acquire) > 1)
            yield();

        while (proc->to_quiesce.load(std::memory_order_acquire) > 0)
            yield();

        proc->lock.unlock();
        thread_exit(exit_code);
        std::unreachable();
    }

    [[noreturn]] void process_exit_signal(int signo, bool core_dumped)
    {
        auto proc = current_process();
        proc->killed_by_signal = true;
        proc->term_signal = signo;
        proc->dumped_core = core_dumped;
        process_exit(0);
    }

    void tick()
    {
        {
            auto &rq = run_queue.unsafe_get();
            const std::unique_lock _ { rq.lock };

            if (auto curr = rq.current)
            {
                if (!curr->is_idle())
                {
                    const auto timer = chrono::main_timer();
                    rq.update_current(timer->ns());
                }
                curr->set_flag(thread_flags::needs_resched);

                if (is_preempt_disabled())
                {
                    const auto timeslice = rq.calc_timeslice(curr->weight);
                    arch::arm_timer_ns(timeslice);
                }
            }
        }

        expire_timeouts();
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
                thread->set_flag(thread_flags::needs_resched);
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
            if (thread->state.load(std::memory_order_acquire) == thread_state::running &&
                !thread->affinity.get(thread->running_on->idx))
                thread->set_flag(thread_flags::needs_resched);
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

        std::vector<process_t *> targets;
        {
            auto locked = members.lock();
            if (locked->empty())
                return -ESRCH;

            targets.reserve(locked->size());
            for (const auto &[_, proc] : *locked)
                targets.push_back(proc);
        }

        bool any_perm = false;
        for (auto proc : targets)
        {
            if (!check_kill(sig, proc))
                continue;

            any_perm = true;
            if (sig == 0)
                continue;

            send_signal(proc, info);
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

        (*group->members.lock())[proc->pid] = proc;
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

        process_t *target_proc = nullptr;
        thread_t *target_thread = nullptr;
        const bool created_proc = !(flags & clone_thread);

        auto cleanup = [&] {
            if (target_thread)
            {
                if (!target_thread->saved_vmspace && target_proc)
                    target_thread->saved_vmspace = target_proc->vmspace;

                const auto tid = target_thread->tid;
                const bool freed_pid = created_proc && target_proc && tid == target_proc->pid;

                delete target_thread;
                if (!freed_pid)
                    free_id(tid);
            }
            if (target_proc)
            {
                if (created_proc)
                {
                    processes.lock()->erase(target_proc->pid);
                    if (target_proc->group)
                        target_proc->group->members.lock()->erase(target_proc->pid);

                    const auto pid = target_proc->pid;
                    delete target_proc;
                    free_id(pid);
                }
                else
                {
                    target_proc->alive_threads.fetch_sub(1, std::memory_order_acq_rel);
                    target_proc->threads.lock()->erase(target_thread->tid);
                }
            }
        };

        if (created_proc)
        {
            target_proc = create_process(
                (flags & clone_parent) ? caller_proc->parent : caller_proc
            );

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
        }
        else target_proc = caller_proc;

        target_thread = create_uthread(
            target_proc, 0, 0, false, true,
            stack_top, caller_thread->nice
        );

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
            (*target_proc->parent->children.lock())[target_proc->pid] = target_proc;

        if (flags & clone_vfork)
            target_proc->vfork_pending = true;

        sched::enqueue_new(target_thread);

        if (flags & clone_vfork)
            caller_proc->vfork_done.wait_unint();

        return target_thread->tid;
    }

    int exec(
        const vfs::path &path, std::vector<std::string> argv,
        std::vector<std::string> envp, std::string pathname
    )
    {
        {
            auto old_thread = current_thread();
            auto process = old_thread->proc;

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

                if (!check_perms(process->cred, inode->stat, access_mode::exec))
                    return -EACCES;
            }

            auto file = vfs::file::create(path, 0, 0);
            auto image = bin::exec::probe(std::move(file));
            if (!image)
                return -lib::map_error(image.error());

            lib::bug_on(!image.value());

            const std::unique_lock _ { process->lock };
            kill_other_threads();

            while (process->alive_threads.load(std::memory_order_acquire) > 1)
                yield();

            while (process->to_quiesce.load(std::memory_order_acquire) > 0)
                yield();

            {
                const std::unique_lock _ { inode->lock };

                std::optional<vfs::file_caps> fcaps;
                if (xattr_caps)
                    fcaps = vfs::parse_file_caps(xattr_caps->span());

                apply_exec_caps(process, inode->stat, fcaps);
            }

            auto new_pmap = std::make_shared<vmm::pagemap>();
            auto new_vmspace = std::make_shared<vmm::vmspace>(std::move(new_pmap));

            preempt_disable();

            process->threads.lock()->erase(old_thread->tid);
            process->alive_threads.fetch_sub(1, std::memory_order_acq_rel);

            auto old_vmspace = process->vmspace;
            process->vmspace = std::move(new_vmspace);

            auto new_thread = (*image)->load({
                .pathname = std::move(pathname),
                .argv = std::move(argv),
                .envp = std::move(envp),
                .proc = process,
            });

            if (!new_thread)
            {
                process->alive_threads.fetch_add(1, std::memory_order_relaxed);
                (*process->threads.lock())[old_thread->tid] = old_thread;

                process->vmspace = std::move(old_vmspace);
                preempt_enable();
                return -ENOEXEC;
            }
            lib::bug_on(new_thread->tid != process->pid);

            old_thread->state.store(thread_state::dead, std::memory_order_release);
            old_thread->saved_vmspace = std::move(old_vmspace);

            if (process->fdt.use_count() > 1)
                process->fdt = process->fdt->clone();
            process->fdt->close_on_exec();

            process->has_execved = true;

            if (process->vfork_pending)
            {
                process->vfork_pending = false;
                process->parent->vfork_done.wake_all();
            }

            sched::enqueue_new(new_thread);
        }
        preempt_enable();

        schedule();
        std::unreachable();
    }

    pid_t waitpid(pid_t wait_pid, int options, int *status)
    {
        const auto proc = current_process();
        const bool no_hang = options & wnohang;

        const auto matches = [&](const process_t *child) {
            if (wait_pid > 0)
                return child->pid == wait_pid;
            if (wait_pid == -1)
                return true;
            if (wait_pid == 0)
                return child->group->pgid == proc->group->pgid;
            return child->group->pgid == -wait_pid;
        };

        while (true)
        {
            {
                auto locked = proc->children.lock();
                if (locked->empty())
                    return -ECHILD;

                for (auto it = locked->begin(); it != locked->end(); it++)
                {
                    auto child = it->second;
                    if (!matches(child))
                        continue;

                    if (!child->is_zombie)
                        continue;

                    const auto cpid = child->pid;
                    const auto code = child->exit_code;
                    const bool killed = child->killed_by_signal;
                    const int term_sig = child->term_signal;
                    const bool core = child->dumped_core;

                    locked->erase(it);
                    (*processes.lock()).erase(cpid);
                    delete child;

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
                        if (!matches(child))
                            continue;

                        const std::unique_lock _ { child->report_lock };

                        if ((options & wuntraced) && child->pending_stop_sig != 0)
                        {
                            const int sig = child->pending_stop_sig;
                            if (!(options & wnowait))
                                child->pending_stop_sig = 0;

                            if (status != nullptr)
                                *status = (sig << 8) | 0x7F;
                            return child->pid;
                        }

                        if ((options & wcontinued) && child->pending_continued)
                        {
                            if (!(options & wnowait))
                                child->pending_continued = false;

                            if (status != nullptr)
                                *status = 0xFFFF;

                            return child->pid;
                        }
                    }
                }

                bool any = false;
                for (auto &[pid, child] : *locked)
                {
                    if (matches(child))
                    {
                        any = true;
                        break;
                    }
                }
                if (!any)
                    return -ECHILD;
            }

            if (no_hang)
                return 0;

            if (proc->wait_child.wait())
                return -EINTR;
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

            if (!check_kill(sig, target))
                return -EPERM;

            if (sig == 0)
                return 0;

            return send_signal(target, info) ? 0 : -ESRCH;
        }

        bool any_perm = false;
        for (auto &[pid, proc] : *processes.lock())
        {
            if (pid == 0 || pid == 1)
                continue;

            if (!check_kill(sig, proc))
                continue;

            any_perm = true;
            if (sig == 0)
                continue;

            send_signal(proc, info);
        }
        return any_perm ? 0 : -EPERM;
    }
} // namespace sched
