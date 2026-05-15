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

    auto wait_queue_t::wait_common(
        std::size_t gen, std::uint64_t ns, wait_mode mode
    ) -> wait_result_t
    {
        auto thread = static_cast<thread_t *>(current_thread());
        const bool kill_aware = (mode != wait_mode::unkillable);

        if (kill_aware && thread->has_flag(thread_flags::kill_pending))
            return { false, false, true };

        if (try_dec_pending())
            return { false, false, false };

        wait_queue_entry_t entry { };

        lock.lock();
        if (try_dec_pending())
        {
            lock.unlock();
            return { false, false, false };
        }
        if (generation.load(std::memory_order_acquire) != gen)
        {
            lock.unlock();
            return { false, false, false };
        }

        const auto sleep_state = (mode == wait_mode::interruptible)
            ? thread_state::sleeping
            : thread_state::blocked;

        entries.push_back(&entry);
        thread->on_wait_queue.store(this, std::memory_order_relaxed);
        thread->wait_entry.store(&entry, std::memory_order_relaxed);
        thread->state.store(sleep_state, std::memory_order_seq_cst);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        const bool killed_now = kill_aware && thread->has_flag(thread_flags::kill_pending);
        const bool interrupted_now = (mode == wait_mode::interruptible) &&
            (thread->has_flag(thread_flags::interrupted) || signal_pending_for(thread));

        if (killed_now || interrupted_now)
        {
            auto expected = sleep_state;
            if (thread->state.compare_exchange_strong(
                expected, thread_state::running,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
                thread->wait_entry.store(nullptr, std::memory_order_relaxed);
                if (entries.find(&entry) != entries.end())
                    entries.remove(&entry);

                lock.unlock();
                if (killed_now)
                    return { false, false, true };

                thread->test_and_clear_flag(thread_flags::interrupted);
                return { true, false, false };
            }
        }

        sleep_entry_t timeout {
            .thread = thread,
            .deadline_ns = 0,
            .expired = false,
            .hook = { }
        };
        if (ns != 0)
            arm_thread_timeout(&timeout, ns);

        lock.unlock();
        schedule();

        const bool interrupted = (mode == wait_mode::interruptible) &&
            thread->test_and_clear_flag(thread_flags::interrupted);
        const bool killed = thread->has_flag(thread_flags::kill_pending);
        if (ns != 0 && !timeout.expired)
            cancel_thread_timeout(&timeout);

        lock.lock();
        thread->on_wait_queue.store(nullptr, std::memory_order_relaxed);
        thread->wait_entry.store(nullptr, std::memory_order_relaxed);
        if (entries.find(&entry) != entries.end())
            entries.remove(&entry);
        lock.unlock();

        return { interrupted, ns != 0 && timeout.expired, killed };
    }

    auto wait_queue_t::wait(std::uint64_t ns) -> wait_result_t
    {
        return wait_common(snapshot_gen(), ns, wait_mode::interruptible);
    }

    auto wait_queue_t::wait_killable(std::uint64_t ns) -> wait_result_t
    {
        return wait_common(snapshot_gen(), ns, wait_mode::killable);
    }

    auto wait_queue_t::wait_unkillable(std::uint64_t ns) -> wait_result_t
    {
        return wait_common(snapshot_gen(), ns, wait_mode::unkillable);
    }

    auto wait_queue_t::wait_prepared(std::size_t gen, std::uint64_t ns) -> wait_result_t
    {
        return wait_common(gen, ns, wait_mode::interruptible);
    }

    auto wait_queue_t::wait_killable_prepared(std::size_t gen, std::uint64_t ns) -> wait_result_t
    {
        return wait_common(gen, ns, wait_mode::killable);
    }

    auto wait_queue_t::wait_unkillable_prepared(std::size_t gen, std::uint64_t ns) -> wait_result_t
    {
        return wait_common(gen, ns, wait_mode::unkillable);
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
