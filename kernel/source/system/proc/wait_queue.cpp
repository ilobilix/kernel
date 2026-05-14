// Copyright (C) 2024-2026  ilobilo

module system.sched.wait_queue;

import system.sched;

namespace sched
{
    namespace
    {
        void visit(auto &type, bool preempt)
        {
            std::visit(lib::overloaded {
                [preempt](thread_base_t *thread) {
                    wake_up(static_cast<thread_t *>(thread), preempt);
                },
                [](wait_queue_entry_t::callback_t &func) {
                    func();
                }
            }, type);
        }
    } // namespace

    thread_base_t *wait_queue_entry_t::current_thread()
    {
        return sched::current_thread();
    }

    bool wait_queue_t::try_dec_pending()
    {
        auto expected = pending.load(std::memory_order_acquire);
        while (expected > 0)
        {
            if (pending.compare_exchange_weak(
                expected, expected - 1,
                std::memory_order_acquire,
                std::memory_order_acquire))
                return true;
        }
        return false;
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

    std::size_t wait_queue_t::snapshot_gen() const
    {
        return generation.load(std::memory_order_acquire);
    }

    auto wait_queue_t::wait(std::uint64_t ns) -> wait_result_t
    {
        return wait_prepared(snapshot_gen(), ns);
    }

    auto wait_queue_t::wait_prepared(std::size_t gen, std::uint64_t ns) -> wait_result_t
    {
        if (try_dec_pending())
            return { false, false };

        wait_queue_entry_t entry { };

        lock.lock();
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_release);
            lock.unlock();
            return { false, false };
        }

        if (generation.load(std::memory_order_acquire) != gen)
        {
            lock.unlock();
            return { false, false };
        }

        entries.push_back(&entry);
        auto thread = static_cast<thread_t *>(std::get<thread_base_t *>(entry.type));

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
            return { interrupted, false };
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

        return { interrupted, timeout.expired };
    }

    auto wait_queue_t::wait_unint(std::uint64_t ns) -> wait_result_t
    {
        return wait_unint_prepared(snapshot_gen(), ns);
    }

    auto wait_queue_t::wait_unint_prepared(std::size_t gen, std::uint64_t ns) -> wait_result_t
    {
        if (try_dec_pending())
            return { false, false };

        wait_queue_entry_t entry { };

        lock.lock();
        if (pending.load(std::memory_order_acquire) > 0)
        {
            pending.fetch_sub(1, std::memory_order_release);
            lock.unlock();
            return { false, false };
        }

        if (generation.load(std::memory_order_acquire) != gen)
        {
            lock.unlock();
            return { false, false };
        }

        entries.push_back(&entry);
        auto thread = static_cast<thread_t *>(std::get<thread_base_t *>(entry.type));

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
            return { false, false };
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

        if (!timeout.expired)
            cancel_thread_timeout(&timeout);

        lock.lock();
        thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
        thread->wait_entry.store(nullptr, std::memory_order_relaxed);
        if (entries.find(&entry) != entries.end())
            entries.remove(&entry);
        lock.unlock();

        return { false, timeout.expired };
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
        auto type = entry->type;
        lock.unlock();

        visit(type, true);
    }

    void wait_queue_t::wake_all()
    {
        decltype(entries) tmp;
        {
            const std::unique_lock _ { lock };
            generation.fetch_add(1, std::memory_order_release);
            tmp = std::move(entries);
        }
        while (!tmp.empty())
            visit(tmp.pop_front()->type, false);
    }
} // namespace sched
