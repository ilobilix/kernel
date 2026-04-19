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
            if (!proc->parent)
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
            send_signal(proc->parent, info);
            proc->parent->wait_child.wake_one();
        }

        void stop_process(process_t *proc, int sig)
        {
            for (auto &[_, thread] : *proc->threads.lock())
            {
                switch (const auto state = thread->state.load(std::memory_order_acquire))
                {
                    case thread_state::running:
                    case thread_state::runnable:
                    case thread_state::sleeping:
                        thread->prev_state.store(state, std::memory_order_relaxed);
                        thread->state.store(thread_state::stopped, std::memory_order_release);
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
                        thread->state.store(prev, std::memory_order_release);
                        break;
                    default:
                        thread->state.store(thread_state::sleeping, std::memory_order_release);
                        wake_up(thread, true);
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

        void drop_queued(process_t *proc, auto &&pred)
        {
            const std::unique_lock _ { proc->sigqueue.lock };
            auto it = proc->sigqueue.queue.begin();
            while (it != proc->sigqueue.queue.end())
            {
                auto next = std::next(it);
                if (pred(it->info.signo))
                {
                    proc->sigqueue.pending.rem(it->info.signo);
                    proc->sigqueue.queue.erase(it);
                }
                it = next;
            }
        }

        std::optional<siginfo_t> dequeue_signal(process_t *proc, int sig)
        {
            std::optional<siginfo_t> info;
            for (auto it = proc->sigqueue.queue.begin(); it != proc->sigqueue.queue.end(); it++)
            {
                if (it->info.signo == sig)
                {
                    info = it->info;
                    proc->sigqueue.queue.erase(it);
                    break;
                }
            }

            bool more = false;
            for (const auto &entry : proc->sigqueue.queue)
            {
                if (entry.info.signo == sig)
                {
                    more = true;
                    break;
                }
            }
            if (!more)
                proc->sigqueue.pending.rem(sig);

            return info;
        }
    } // namespace

    bool send_signal(thread_t *thread, const siginfo_t &info)
    {
        const int sig = info.signo;
        if (sig < 1 || sig > static_cast<int>(nsig))
            return false;

        auto proc = thread->proc;

        if (sig == sigcont)
            drop_queued(proc, is_stop_signal);
        else if (is_stop_signal(sig))
            drop_queued(proc, [](int sig) { return sig == sigcont; });

        if (is_ignored(proc, sig))
        {
            if (sig == sigcont)
                continue_process(proc);
            return true;
        }

        {
            const std::unique_lock _ { proc->sigqueue.lock };

            const bool already = proc->sigqueue.pending.has(sig);
            if (sig >= sigrtmin || !already)
            {
                proc->sigqueue.pending.add(sig);
                proc->sigqueue.queue.emplace_back(info);
            }
        }

        if (sig == sigcont)
            continue_process(proc);

        thread->set_flag(thread_flags::signal_pending);
        wake_for_signal(thread, sig);
        return true;
    }

    void flush_signal(process_t *proc, int sig)
    {
        if (sig < 1 || sig > static_cast<int>(nsig))
            return;

        drop_queued(proc, [sig](int s) { return s == sig; });
    }

    bool send_signal(process_t *process, const siginfo_t &info)
    {
        const int sig = info.signo;
        if (sig < 1 || sig > static_cast<int>(nsig))
            return false;

        thread_t *target = nullptr;
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

            if (target == nullptr)
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

        if (target == nullptr)
            return false;

        return send_signal(target, info);
    }

    std::uintptr_t sigreturn()
    {
        auto thread = current_thread();
        if (!arch::restore_sigframe(thread, thread->saved_regs))
            process_exit_signal(sigsegv);
        return thread->saved_regs->ret();
    }

    bool consume_pending_stops()
    {
        auto thread = current_thread();
        auto proc = thread->proc;

        bool stopped = false;
        while (true)
        {
            int sig = 0;
            {
                const std::unique_lock _ { proc->sigqueue.lock };
                sig = (proc->sigqueue.pending & ~thread->sigmask).lowest();
                if (sig == 0)
                    break;
            }

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
                {
                    const std::unique_lock _ { proc->sigqueue.lock };
                    dequeue_signal(proc, sig);
                }
                stop_process(proc, sig);
                yield();
                stopped = true;
                continue;
            }

            if (da == default_action::ignore || da == default_action::cont)
            {
                const std::unique_lock _ { proc->sigqueue.lock };
                dequeue_signal(proc, sig);
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
        auto proc = thread->proc;

        while (true)
        {
            int sig = 0;
            std::optional<siginfo_t> info;
            {
                const std::unique_lock _ { proc->sigqueue.lock };
                sig = (proc->sigqueue.pending & ~thread->sigmask).lowest();
                if (sig == 0)
                    break;

                info = dequeue_signal(proc, sig);
            }

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

            break;
        }

        {
            const std::unique_lock _ { proc->sigqueue.lock };
            if (!(proc->sigqueue.pending & ~thread->sigmask).any())
                thread->clear_flag(thread_flags::signal_pending);
        }
    }
} // namespace sched
