// Copyright (C) 2024-2026  ilobilo

module system.sched;

import system.chrono;

namespace sched
{
    namespace
    {
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

        lib::locker<
            lib::rbtree<
                alarm_entry_t,
                &alarm_entry_t::hook,
                lib::compare<
                    alarm_entry_t,
                    std::uint64_t,
                    &alarm_entry_t::deadline_ns
                >
            >, lib::spinlock_irq
        > alarm_list;

        bool consume_itimer(cpu_itimer_t &it, std::uint64_t delta_ns)
        {
            const std::unique_lock _ { it.lock };
            if (it.value_ns == 0)
                return false;

            if (delta_ns < it.value_ns)
            {
                it.value_ns -= delta_ns;
                return false;
            }

            it.value_ns = it.interval_ns;
            return true;
        }

        void send_itimer_signal(process_t *proc, int signo)
        {
            siginfo_t info {
                .signo = signo,
                .code = si_kernel,
                .err = 0,
                .pid = 0,
                .uid = 0,
                .status = 0,
                .addr = 0,
                .value = 0,
            };
            send_signal(proc, info);
        }
    } // namespace

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

        if (locked->contains(entry))
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
            thread->state.store(thread_state::sleeping, std::memory_order_relaxed);
        }

        yield();

        if (!entry.expired)
        {
            auto locked = sleep_list.lock();
            locked->remove(&entry);
        }

        const auto now = timer->ns();
        return now >= deadline ? 0 : deadline - now;
    }

    void sleep()
    {
        auto thread = current_thread();
        thread->state.store(thread_state::sleeping, std::memory_order_release);
        schedule();
    }

    void block()
    {
        auto thread = current_thread();
        thread->state.store(thread_state::blocked, std::memory_order_release);
        schedule();
    }

    void expire_timeouts()
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

    std::uint64_t arm_alarm(
        alarm_entry_t *entry, process_t *proc,
        std::uint64_t ns, std::uint64_t interval_ns
    )
    {
        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        auto locked = alarm_list.lock();

        std::uint64_t remaining = 0;
        if (entry->armed)
        {
            locked->remove(entry);
            if (entry->deadline_ns > now)
                remaining = entry->deadline_ns - now;
        }

        entry->proc = proc;
        entry->deadline_ns = now + ns;
        entry->interval_ns = interval_ns;
        entry->armed = true;
        entry->expired = false;
        locked->insert(entry);

        return remaining;
    }

    std::uint64_t cancel_alarm(alarm_entry_t *entry)
    {
        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        auto locked = alarm_list.lock();
        if (!entry->armed)
            return 0;

        locked->remove(entry);
        entry->armed = false;
        entry->interval_ns = 0;

        return entry->deadline_ns > now ? entry->deadline_ns - now : 0;
    }

    alarm_state_t alarm_state(alarm_entry_t *entry)
    {
        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        auto locked = alarm_list.lock();
        if (!entry->armed)
            return { 0, entry->interval_ns };

        const auto remaining = entry->deadline_ns > now ? entry->deadline_ns - now : 0;
        return { remaining, entry->interval_ns };
    }

    void charge_cpu_itimers(process_t *proc, std::uint64_t delta_ns, bool from_user)
    {
        if (proc == nullptr || delta_ns == 0)
            return;

        if (from_user && consume_itimer(proc->itimer_virtual, delta_ns))
            send_itimer_signal(proc, sigvtalrm);

        if (consume_itimer(proc->itimer_prof, delta_ns))
            send_itimer_signal(proc, sigprof);
    }

    void expire_alarms()
    {
        const auto timer = chrono::main_timer();
        const auto now = timer->ns();

        auto locked = alarm_list.lock();
        auto it = locked->begin();
        while (it != locked->end())
        {
            auto entry = (it++).value();
            if (entry->deadline_ns > now)
                break;

            locked->remove(entry);

            if (entry->interval_ns > 0)
            {
                entry->deadline_ns = now + entry->interval_ns;
                entry->expired = false;
                locked->insert(entry);
            }
            else
            {
                entry->armed = false;
                entry->expired = true;
            }

            siginfo_t info {
                .signo = sigalrm,
                .code = si_kernel,
                .err = 0,
                .pid = 0,
                .uid = 0,
                .status = 0,
                .addr = 0,
                .value = 0,
            };
            send_signal(entry->proc, info);
        }
    }
} // namespace sched
