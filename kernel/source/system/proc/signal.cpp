// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.cpu.regs;
import system.cpu.local;
import lib;
import std;

namespace sched
{
    namespace
    {
        void wake_for_signal(thread_t *thread, int sig)
        {
            switch (thread->state.load(std::memory_order_acquire))
            {
                case thread_state::sleeping:
                    thread->set_flag(thread_flags::interrupted);
                    wake_up(thread, true);
                    break;
                case thread_state::stopped:
                    if (sig == sigkill)
                        wake_up(thread, true, true);
                    else if (sig == sigcont)
                        wake_up(thread, true);
                    break;
                case thread_state::running:
                    if (auto on = thread->running_on)
                    {
                        thread->set_flag(thread_flags::needs_resched);
                        if (on != &cpu::self().unsafe_get())
                            arch::wake_up_other(on->idx);
                    }
                    break;
                default:
                    break;
            }
        }

        void notify_parent(process_t *proc, int code, int status)
        {
            auto parent = proc->parent.lock();
            if (!parent)
                return;

            siginfo_t info {
                .signo = sigchld,
                .code = code,
                .err = 0,
                .pid = proc->pid,
                .uid = 0,
                .status = status,
                .addr = 0,
                .value = 0
            };
            send_signal(parent.get(), info);
            parent->wait_child.wake_one();
        }

        void stop_process(process_t *proc, int sig)
        {
            for (auto &[_, thread] : *proc->threads.lock())
            {
                auto state = thread->state.load(std::memory_order_acquire);
                while (true)
                {
                    if (state != thread_state::running &&
                        state != thread_state::runnable &&
                        state != thread_state::sleeping)
                        break;

                    thread->prev_state.store(state, std::memory_order_relaxed);
                    if (!thread->state.compare_exchange_weak(
                        state, thread_state::stopped,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                        continue;

                    if (state == thread_state::running)
                    {
                        if (auto on = thread->running_on)
                        {
                            thread->set_flag(thread_flags::needs_resched);
                            if (on != &cpu::self().unsafe_get())
                                arch::wake_up_other(on->idx);
                        }
                    }
                    else if (state == thread_state::runnable)
                        dequeue_stopped(thread.get());
                    break;
                }
            }

            {
                const std::unique_lock _ { proc->report_lock };
                proc->pending_stop_sig = sig;
                proc->pending_continued = false;
            }
            notify_parent(proc, cld_stopped, sig);
        }

        void continue_process(process_t *proc)
        {
            for (auto &[_, thread] : *proc->threads.lock())
            {
                if (thread->state.load(std::memory_order_acquire) != thread_state::stopped)
                    continue;

                switch (const auto prev = thread->prev_state.load(std::memory_order_relaxed))
                {
                    case thread_state::sleeping:
                    case thread_state::blocked:
                    {
                        auto expected = thread_state::stopped;
                        thread->state.compare_exchange_strong(
                            expected, prev,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire
                        );
                        break;
                    }
                    default:
                        wake_stopped(thread.get());
                        break;
                }
            }

            {
                const std::unique_lock _ { proc->report_lock };
                proc->pending_stop_sig = 0;
                proc->pending_continued = true;
            }
            notify_parent(proc, cld_continued, sigcont);
        }

        bool is_stop_signal(int sig)
        {
            return sig == sigstop || sig == sigtstp || sig == sigttin || sig == sigttou;
        }

        bool is_ignored(process_t *proc, int sig)
        {
            if (sig == sigkill || sig == sigstop)
                return false;

            const std::unique_lock _ { proc->sigactions->lock };
            const auto &act = proc->sigactions->actions[sig - 1];

            if (act.handler == sig_ign)
                return true;

            if (act.handler == sig_dfl && default_for(sig) == default_action::ignore)
                return true;

            return false;
        }

        void drop_queued(signal_queue_t &sq, auto &&pred)
        {
            const std::unique_lock _ { sq.lock };
            auto it = sq.queue.begin();
            while (it != sq.queue.end())
            {
                auto next = std::next(it);
                if (pred(it->info.signo))
                {
                    sq.pending.rem(it->info.signo);
                    sq.queue.erase(it);
                }
                it = next;
            }
        }

        void drop_queued_all(process_t *proc, auto &&pred)
        {
            drop_queued(proc->sigqueue, pred);
            for (auto &[_, thread] : *proc->threads.lock())
                drop_queued(thread->sigqueue, pred);
        }

        std::optional<siginfo_t> dequeue_from(signal_queue_t &sq, int sig)
        {
            std::optional<siginfo_t> info;
            for (auto it = sq.queue.begin(); it != sq.queue.end(); it++)
            {
                if (it->info.signo == sig)
                {
                    info = it->info;
                    sq.queue.erase(it);
                    break;
                }
            }

            bool more = false;
            for (const auto &entry : sq.queue)
            {
                if (entry.info.signo == sig)
                {
                    more = true;
                    break;
                }
            }
            if (!more)
                sq.pending.rem(sig);

            return info;
        }

        int lowest_deliverable(thread_t *thread)
        {
            {
                const std::unique_lock _ { thread->sigqueue.lock };
                if (const auto sig = (thread->sigqueue.pending & ~thread->sigmask).lowest(); sig != 0)
                    return sig;
            }

            auto proc = thread->proc.get();
            const std::unique_lock _ { proc->sigqueue.lock };
            return (proc->sigqueue.pending & ~thread->sigmask).lowest();
        }

        std::optional<siginfo_t> take_signal(thread_t *thread, int sig)
        {
            {
                const std::unique_lock _ { thread->sigqueue.lock };
                if (thread->sigqueue.pending.has(sig))
                    return dequeue_from(thread->sigqueue, sig);
            }

            auto proc = thread->proc.get();
            const std::unique_lock _ { proc->sigqueue.lock };
            if (proc->sigqueue.pending.has(sig))
                return dequeue_from(proc->sigqueue, sig);

            return std::nullopt;
        }

        bool queue_signal(signal_queue_t &sq, const siginfo_t &info)
        {
            const std::unique_lock _ { sq.lock };

            if (info.signo >= sigrtmin || !sq.pending.has(info.signo))
            {
                sq.pending.add(info.signo);
                sq.queue.emplace_back(info);
            }
            return true;
        }

        void wake_signal_waiters(process_t *proc, int sig)
        {
            auto locked = proc->sig_waiters.lock();
            for (auto it = locked->begin(); it != locked->end(); it++)
            {
                if (it->interest.has(sig) && it->wake)
                    it->wake();
            }
        }
    } // namespace

    bool signal_pending_for(thread_t *thread)
    {
        {
            const std::unique_lock _ { thread->sigqueue.lock };
            if ((thread->sigqueue.pending & ~thread->sigmask).any())
                return true;
        }

        auto proc = thread->proc.get();
        const std::unique_lock _ { proc->sigqueue.lock };
        return (proc->sigqueue.pending & ~thread->sigmask).any();
    }

    std::optional<siginfo_t> dequeue_signal(process_t *proc, const sigset_t &set)
    {
        const std::unique_lock _ { proc->sigqueue.lock };
        const auto sig = (proc->sigqueue.pending & set).lowest();
        if (sig == 0)
            return std::nullopt;
        return dequeue_from(proc->sigqueue, sig);
    }

    std::optional<siginfo_t> dequeue_signal(thread_t *thread, const sigset_t &set)
    {
        {
            const std::unique_lock _ { thread->sigqueue.lock };
            const auto sig = (thread->sigqueue.pending & set).lowest();
            if (sig != 0)
                return dequeue_from(thread->sigqueue, sig);
        }
        return dequeue_signal(thread->proc.get(), set);
    }

    void add_signal_waiter(process_t *proc, signal_waiter_t &waiter)
    {
        auto locked = proc->sig_waiters.lock();
        if (locked->find(&waiter) == locked->end())
            locked->push_back(&waiter);
    }

    void remove_signal_waiter(process_t *proc, signal_waiter_t &waiter)
    {
        auto locked = proc->sig_waiters.lock();
        if (locked->find(&waiter) != locked->end())
            locked->remove(&waiter);
    }

    void update_signal_waiter(
        process_t *proc, signal_waiter_t &waiter,
        const sigset_t &interest
    )
    {
        const auto guard = proc->sig_waiters.lock();
        waiter.interest = interest;
    }

    bool send_signal(thread_t *thread, const siginfo_t &info)
    {
        const int sig = info.signo;
        if (sig < 1 || sig > static_cast<int>(nsig))
            return false;

        auto proc = thread->proc.get();

        if (sig == sigcont)
            drop_queued_all(proc, is_stop_signal);
        else if (is_stop_signal(sig))
            drop_queued_all(proc, [](int sig) { return sig == sigcont; });

        if (!thread->sigmask.has(sig) && is_ignored(proc, sig))
        {
            if (sig == sigcont)
                continue_process(proc);
            return true;
        }

        queue_signal(thread->sigqueue, info);

        if (sig == sigcont)
            continue_process(proc);

        thread->set_flag(thread_flags::signal_pending);
        std::atomic_thread_fence(std::memory_order_seq_cst);

        wake_for_signal(thread, sig);
        wake_signal_waiters(proc, sig);
        return true;
    }

    bool force_signal(thread_t *thread, const siginfo_t &info)
    {
        const int sig = info.signo;
        if (sig < 1 || sig > static_cast<int>(nsig))
            return false;

        auto proc = thread->proc.get();
        {
            const std::unique_lock _ { proc->sigactions->lock };
            auto &action = proc->sigactions->actions[sig - 1];
            if (action.handler == sig_ign || thread->sigmask.has(sig))
                action = sigaction_t { };
        }
        thread->sigmask.rem(sig);

        return send_signal(thread, info);
    }

    void flush_signal(process_t *proc, int sig)
    {
        if (sig < 1 || sig > static_cast<int>(nsig))
            return;

        drop_queued_all(proc, [sig](int s) { return s == sig; });
    }

    bool send_signal(process_t *process, const siginfo_t &info)
    {
        const int sig = info.signo;
        if (sig < 1 || sig > static_cast<int>(nsig))
            return false;

        std::shared_ptr<thread_t> target;
        {
            auto locked = process->threads.lock();
            for (auto &[_, thread] : *locked)
            {
                if (thread->state.load(std::memory_order_acquire) == thread_state::dead)
                    continue;

                if (!thread->sigmask.has(sig))
                {
                    target = thread;
                    break;
                }
            }

            if (!target)
            {
                for (auto &[_, thread] : *locked)
                {
                    if (thread->state.load(std::memory_order_acquire) != thread_state::dead)
                    {
                        target = thread;
                        break;
                    }
                }
            }
        }

        if (!target)
            return false;

        if (sig == sigcont)
            drop_queued_all(process, is_stop_signal);
        else if (is_stop_signal(sig))
            drop_queued_all(process, [](int sig) { return sig == sigcont; });

        if (!target->sigmask.has(sig) && is_ignored(process, sig))
        {
            if (sig == sigcont)
                continue_process(process);
            return true;
        }

        queue_signal(process->sigqueue, info);

        if (sig == sigcont)
            continue_process(process);

        target->set_flag(thread_flags::signal_pending);
        std::atomic_thread_fence(std::memory_order_seq_cst);

        wake_for_signal(target.get(), sig);
        wake_signal_waiters(process, sig);
        return true;
    }

    std::uintptr_t sigreturn()
    {
        auto thread = current_thread();
        if (!arch::restore_sigframe(thread, thread->saved_regs))
            process_exit_signal(sigsegv);
        return thread->saved_regs->ret();
    }

    std::size_t min_altstack_size()
    {
        return arch::min_altstack_size();
    }

    void scoped_sigmask::apply(const sigset_t *mask)
    {
        if (!mask)
            return;

        thread = current_thread();
        auto kmask = *mask;
        kmask &= ~sigmask_uncatchable;

        thread->saved_sigmask = thread->sigmask;
        thread->sigmask = kmask;
        armed = true;
    }

    scoped_sigmask::~scoped_sigmask()
    {
        if (armed && thread && thread->saved_sigmask.has_value())
        {
            thread->sigmask = *thread->saved_sigmask;
            thread->saved_sigmask = std::nullopt;
        }
    }

    bool consume_pending_stops()
    {
        auto thread = current_thread();
        auto proc = thread->proc.get();

        bool stopped = false;
        while (true)
        {
            const int sig = lowest_deliverable(thread);
            if (sig == 0)
                break;

            sigaction_t action;
            {
                const std::unique_lock _ { proc->sigactions->lock };
                action = proc->sigactions->actions[sig - 1];
            }

            if (action.handler != sig_dfl)
                break;

            const auto da = default_for(sig);
            if (da == default_action::stop)
            {
                take_signal(thread, sig);
                stop_process(proc, sig);
                yield();
                stopped = true;
                continue;
            }

            if (da == default_action::ignore || da == default_action::cont)
            {
                take_signal(thread, sig);
                continue;
            }

            break;
        }

        thread->test_and_clear_flag(thread_flags::interrupted);
        return stopped;
    }

    void handle_pending_signals(cpu::registers *regs)
    {
        if (!arch::in_user_mode(regs))
            return;

        auto thread = current_thread();
        auto proc = thread->proc.get();

        bool delivered = false;
        while (true)
        {
            const int sig = lowest_deliverable(thread);
            if (sig == 0)
                break;

            const auto info = take_signal(thread, sig);
            if (!info)
                continue;

            sigaction_t action;
            {
                const std::unique_lock _ { proc->sigactions->lock };
                action = proc->sigactions->actions[sig - 1];

                if ((action.flags & sa_resethand) && action.handler != sig_dfl &&
                    action.handler != sig_ign)
                    proc->sigactions->actions[sig - 1].handler = sig_dfl;
            }

            if (action.handler == sig_ign)
                continue;

            if (action.handler == sig_dfl)
            {
                switch (default_for(sig))
                {
                    case default_action::ignore:
                        continue;
                    case default_action::term:
                        process_exit_signal(sig);
                    case default_action::core:
                        process_exit_signal(sig, true);
                    case default_action::stop:
                        stop_process(proc, sig);
                        yield();
                        continue;
                    case default_action::cont:
                        continue;
                }
                continue;
            }

            if (!arch::setup_sigframe(thread, regs, sig, *info, action))
                process_exit_signal(sigsegv);

            delivered = true;
            break;
        }

        if (!delivered && thread->saved_sigmask.has_value())
        {
            thread->sigmask = *thread->saved_sigmask;
            thread->saved_sigmask = std::nullopt;
        }

        if (!signal_pending_for(thread))
            thread->clear_flag(thread_flags::signal_pending);
    }
} // namespace sched
