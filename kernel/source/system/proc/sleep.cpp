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
} // namespace sched
