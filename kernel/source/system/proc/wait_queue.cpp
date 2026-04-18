// Copyright (C) 2024-2026  ilobilo

module system.sched.wait_queue;

import system.sched;

namespace sched
{
    thread_base_t *wait_queue_entry_t::current_thread()
    {
        return sched::current_thread();
    }

    void wait_queue_t::add_entry(wait_queue_entry_t &entry)
    {
        const std::unique_lock _ { lock };
        entries.push_back(&entry);
    }

    void wait_queue_t::remove_entry(wait_queue_entry_t &entry)
    {
        const std::unique_lock _ { lock };
        if (entries.find(&entry) != entries.end())
            entries.remove(&entry);
    }

    void wait_queue_t::unlink_atomic(
        std::atomic<wait_queue_t *> &on_queue_ref,
        std::atomic<wait_queue_entry_t *> &entry_ref
    )
    {
        const std::unique_lock _ { lock };
        if (on_queue_ref.load(std::memory_order_relaxed) != this)
            return;

        if (auto entry = entry_ref.load(std::memory_order_relaxed))
        {
            if (entries.find(entry) != entries.end())
                entries.remove(entry);
        }

        on_queue_ref.store(nullptr, std::memory_order_relaxed);
        entry_ref.store(nullptr, std::memory_order_relaxed);
    }

    bool wait_queue_t::wait(std::uint64_t ns)
    {
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_acquire);
            return false;
        }

        wait_queue_entry_t entry { };

        lock.lock();
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_release);
            lock.unlock();
            return false;
        }

        entries.push_back(&entry);
        auto thread = static_cast<thread_t *>(entry.thread);

        thread->state.store(thread_state::sleeping, std::memory_order_relaxed);
        thread->on_wait_queue.store(this, std::memory_order_relaxed);
        thread->wait_entry.store(&entry, std::memory_order_relaxed);

        if (ns == 0)
        {
            lock.unlock();
            schedule();
            const bool interrupted = thread->test_and_clear_flag(thread_flags::interrupted);
            lock.lock();

            thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
            thread->wait_entry.store(nullptr, std::memory_order_relaxed);

            if (entries.find(&entry) != entries.end())
                entries.remove(&entry);
            lock.unlock();
            return interrupted;
        }

        sleep_entry_t timeout {
            .thread = thread,
            .deadline_ns = 0,
            .expired = false,
            .hook = { }
        };
        arm_thread_timeout(&timeout, ns);

        lock.unlock();
        schedule();
        const bool interrupted = thread->test_and_clear_flag(thread_flags::interrupted);
        if (!timeout.expired)
            cancel_thread_timeout(&timeout);

        lock.lock();
        thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
        thread->wait_entry.store(nullptr, std::memory_order_relaxed);

        if (entries.find(&entry) != entries.end())
            entries.remove(&entry);
        lock.unlock();

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
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_release);
            lock.unlock();
            return;
        }

        entries.push_back(&entry);
        auto thread = static_cast<thread_t *>(entry.thread);

        thread->state.store(thread_state::sleeping, std::memory_order_relaxed);
        thread->on_wait_queue.store(this, std::memory_order_relaxed);
        thread->wait_entry.store(&entry, std::memory_order_relaxed);

        if (ns == 0)
        {
            lock.unlock();
            schedule();
            lock.lock();

            thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
            thread->wait_entry.store(nullptr, std::memory_order_relaxed);

            if (entries.find(&entry) != entries.end())
                entries.remove(&entry);
            lock.unlock();
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
        schedule();

        if (!timeout.expired)
            cancel_thread_timeout(&timeout);

        lock.lock();
        thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
        thread->wait_entry.store(nullptr, std::memory_order_relaxed);
        if (entries.find(&entry) != entries.end())
            entries.remove(&entry);
        lock.unlock();
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
        auto thread = static_cast<thread_t *>(entry->thread);
        lock.unlock();
        wake_up(thread, true);
    }

    void wait_queue_t::wake_all()
    {
        const std::unique_lock _ { lock };
        if (!entries.empty())
        {
            while (!entries.empty())
            {
                auto entry = entries.pop_front();
                wake_up(static_cast<thread_t *>(entry->thread), false);
            }
        }
        else pending.fetch_add(1, std::memory_order_release);
    }
} // namespace sched
