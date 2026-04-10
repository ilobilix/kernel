// Copyright (C) 2024-2026  ilobilo

module system.sched.wait_queue;

import system.sched;

namespace sched
{
    thread_base_t *wait_queue_entry_t::current_thread()
    {
        return sched::current_thread();
    }

    void wait_queue_t::prepare_wait(wait_queue_entry_t *entry, bool uninterruptible)
    {
        auto locked = entries.lock();
        locked->push_back(entry);
        static_cast<thread_t *>(entry->thread)->state = uninterruptible
            ? thread_state::blocked
            : thread_state::sleeping;
    }

    void wait_queue_t::finish_wait(wait_queue_entry_t *entry)
    {
        auto locked = entries.lock();
        locked->remove(entry);
    }

    bool wait_queue_t::wait(std::uint64_t ns)
    {
        wait_queue_entry_t entry { };
        prepare_wait(&entry, false);

        if (ns == 0)
            return sched::yield();

        sleep_entry_t timeout {
            .thread = static_cast<thread_t *>(entry.thread),
            .deadline_ns = 0,
            .expired = false,
            .hook = { }
        };
        arm_thread_timeout(&timeout, ns);

        const bool interrupted = sched::yield();
        if (timeout.expired)
            finish_wait(&entry);
        else
            cancel_thread_timeout(&timeout);

        return interrupted;
    }

    void wait_queue_t::wait_unint(std::uint64_t ns)
    {
        wait_queue_entry_t entry { };
        prepare_wait(&entry, true);

        if (ns == 0)
        {
            sched::yield();
            return;
        }

        sleep_entry_t timeout {
            .thread = static_cast<thread_t *>(entry.thread),
            .deadline_ns = 0,
            .expired = false,
            .hook = { }
        };
        arm_thread_timeout(&timeout, ns);

        sched::yield();
        if (timeout.expired)
            finish_wait(&entry);
        else
            cancel_thread_timeout(&timeout);
    }

    void wait_queue_t::wake_one()
    {
        auto locked = entries.lock();
        if (locked->empty())
            return;
        auto entry = locked->pop_front();
        wake_up(static_cast<thread_t *>(entry->thread), true);
    }

    void wait_queue_t::wake_all()
    {
        auto locked = entries.lock();
        while (!locked->empty())
        {
            auto entry = locked->pop_front();
            wake_up(static_cast<thread_t *>(entry->thread), true);
        }
    }

    void wait_queue_t::wake_exclusive()
    {
        auto locked = entries.lock();
        auto it = locked->begin();
        while (it != locked->end())
        {
            auto entry = (it++).value();
            locked->remove(entry);

            const bool exclusive = entry->exclusive;
            wake_up(static_cast<thread_t *>(entry->thread), true);

            if (exclusive)
                break;
        }
    }
} // namespace sched
