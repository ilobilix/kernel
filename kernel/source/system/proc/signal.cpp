// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.cpu.regs;
import lib;
import std;

namespace sched
{
    namespace
    {
        void wake_for_signal(thread_t *thread, int sig)
        {
            switch (thread->state)
            {
                case thread_state::sleeping:
                    thread->flags |= thread_flags::interrupted;
                    wake_up(thread, true);
                    break;
                case thread_state::stopped:
                    if (sig == sigkill || sig == sigcont)
                        wake_up(thread, true);
                    break;
                case thread_state::running:
                    if (thread->running_on)
                    {
                        thread->flags |= thread_flags::needs_resched;
                        arch::wake_up_other(thread->running_on->idx);
                    }
                    break;
                default:
                    break;
            }
        }

        void stop_process(process_t *proc)
        {
            for (auto &[_, thread] : *proc->threads.lock())
            {
                switch (thread->state)
                {
                    case thread_state::running:
                    case thread_state::runnable:
                    case thread_state::sleeping:
                        thread->prev_state = thread->state;
                        thread->state = thread_state::stopped;
                        if (thread->running_on)
                        {
                            thread->flags |= thread_flags::needs_resched;
                            arch::wake_up_other(thread->running_on->idx);
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        void continue_process(process_t *proc)
        {
            for (auto &[_, thread] : *proc->threads.lock())
            {
                if (thread->state != thread_state::stopped)
                    continue;

                switch (thread->prev_state)
                {
                    case thread_state::sleeping:
                    case thread_state::blocked:
                        thread->state = thread->prev_state;
                        break;
                    default:
                        thread->state = thread_state::sleeping;
                        wake_up(thread, true);
                        break;
                }
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
        {
            const std::unique_lock _ { proc->sigqueue.lock };

            const bool already = proc->sigqueue.pending.has(sig);
            if (sig >= sigrtmin || !already)
            {
                proc->sigqueue.pending.add(sig);
                proc->sigqueue.queue.emplace_back(info);
            }
        }

        thread->flags |= thread_flags::signal_pending;
        wake_for_signal(thread, sig);
        return true;
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
                if (thread->state == thread_state::dead)
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
                    if (thread->state != thread_state::dead)
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
            process_exit(128 + sigsegv);
        return thread->saved_regs->rax;
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
                    case default_action::core:
                        process_exit(128 + sig);
                    case default_action::stop:
                        stop_process(proc);
                        yield();
                        continue;
                    case default_action::cont:
                        continue_process(proc);
                        continue;
                }
                continue;
            }

            if (!arch::setup_sigframe(thread, regs, sig, *info, action))
                process_exit(128 + sigsegv);

            break;
        }

        {
            const std::unique_lock _ { proc->sigqueue.lock };
            if (!(proc->sigqueue.pending & ~thread->sigmask).any())
                thread->flags &= ~thread_flags::signal_pending;
        }
    }
} // namespace sched
