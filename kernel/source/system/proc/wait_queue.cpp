// Copyright (C) 2024-2026  ilobilo

module system.sched.wait_queue;

import system.sched;

namespace sched
{
    thread_base_t *wait_queue_entry_t::current_thread()
    {
        return sched::current_thread();
    }

    bool wait_queue_t::wait(std::uint64_t ns)
    {
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_acquire);
            return true;
        }

        wait_queue_entry_t entry { };

        lock.lock();
        auto thread = static_cast<thread_t *>(entry.thread);
        entries.push_back(&entry);
        thread->state = thread_state::sleeping;

        if (ns == 0)
        {
            lock.unlock();
            return sched::yield();
        }

        sleep_entry_t timeout {
            .thread = thread,
            .deadline_ns = 0,
            .expired = false,
            .hook = { }
        };
        arm_thread_timeout(&timeout, ns);

        lock.unlock();
        const bool interrupted = sched::yield();
        if (timeout.expired)
        {
            lock.lock();
            entries.remove(&entry);
            lock.unlock();
        }
        else cancel_thread_timeout(&timeout);

        return interrupted;
    }

    void wait_queue_t::wait_unint(std::uint64_t ns)
    {
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_acquire);
            return;
        }

        wait_queue_entry_t entry { };

        lock.lock();
        entries.push_back(&entry);
        static_cast<thread_t *>(entry.thread)->state = thread_state::blocked;

        if (ns == 0)
        {
            lock.unlock();
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

        lock.unlock();
        sched::yield();

        if (timeout.expired)
        {
            lock.lock();
            entries.remove(&entry);
            lock.unlock();
        }
        else cancel_thread_timeout(&timeout);
    }

    void wait_queue_t::wake_one(bool drop)
    {
        lock.lock();
        if (entries.empty())
        {
            if (!drop)
                pending.fetch_add(1, std::memory_order_release);
            lock.unlock();
            return;
        }
        auto entry = entries.pop_front();
        lock.unlock();
        wake_up(static_cast<thread_t *>(entry->thread), true);
    }

    void wait_queue_t::wake_all()
    {
        lock.lock();
        while (!entries.empty())
        {
            auto entry = entries.pop_front();
            wake_up(static_cast<thread_t *>(entry->thread), false);
        }
        lock.unlock();
    }
} // namespace sched
