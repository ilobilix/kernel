// Copyright (C) 2024-2026  ilobilo

export module lib:spinlock;

import :bug_on;

import std;

namespace lib
{
    enum class lock_type
    {
        preempt,
        irq
    };

    namespace lock
    {
        void acquire_preempt();
        void release_preempt();

        void pause();
    } // namespace lock
} // namespace lib

export namespace lib
{
    namespace lock
    {
        void acquire_irq();
        void release_irq();
    } // namespace lock

    template<lock_type Type>
    class spinlock_base
    { };

    template<>
    class spinlock_base<lock_type::preempt>
    {
        private:
        std::atomic_size_t _next_ticket;
        std::atomic_size_t _serving_ticket;

        protected:
        void acquire()
        {
            const auto ticket = _next_ticket.fetch_add(1, std::memory_order_relaxed);
            while (_serving_ticket.load(std::memory_order_acquire) != ticket)
                lock::pause();
        }

        bool release()
        {
            if (is_locked() == false)
                return false;

            const auto current = _serving_ticket.load(std::memory_order_relaxed);
            _serving_ticket.store(current + 1, std::memory_order_release);
            return true;
        }

        public:
        constexpr spinlock_base() : _next_ticket { 0 }, _serving_ticket { 0 } { }

        spinlock_base(const spinlock_base &) = delete;
        spinlock_base(spinlock_base &&) = delete;

        spinlock_base &operator=(const spinlock_base &) = delete;
        spinlock_base &operator=(spinlock_base &&) = delete;

        void lock()
        {
            lock::acquire_preempt();
            acquire();
        }

        bool unlock()
        {
            if (!release())
                return false;

            lock::release_preempt();
            return true;
        }

        [[nodiscard]] bool is_locked() const
        {
            const auto current = _serving_ticket.load(std::memory_order_relaxed);
            const auto next = _next_ticket.load(std::memory_order_relaxed);
            return current != next;
        }

        [[nodiscard]] bool try_lock()
        {
            if (is_locked())
                return false;

            lock();
            return true;
        }
    };

    template<>
    class spinlock_base<lock_type::irq> : public spinlock_base<lock_type::preempt>
    {
        public:
        constexpr spinlock_base() : spinlock_base<lock_type::preempt> { } { }

        using spinlock_base<lock_type::preempt>::spinlock_base;

        void lock()
        {
            lock::acquire_irq();
            acquire();
        }

        bool unlock()
        {
            if (!release())
                return false;

            lock::release_irq();
            return true;
        }
    };

    template<lock_type Type>
    class rwlock_base
    {
        private:
        enum flags : std::size_t
        {
            writer_held = 1uz << 0,
            writer_wait = 1uz << 1,
            reader_unit = 1uz << 2,
            writer_mask = writer_held | writer_wait
        };

        std::atomic_size_t state;
        spinlock_base<lock_type::preempt> wlock;

        static void acquire()
        {
            if constexpr (Type == lock_type::irq)
                lock::acquire_irq();
            else if constexpr (Type == lock_type::preempt)
                lock::acquire_preempt();
        }

        static void release()
        {
            if constexpr (Type == lock_type::irq)
                lock::release_irq();
            else if constexpr (Type == lock_type::preempt)
                lock::release_preempt();
        }

        public:
        constexpr rwlock_base() : state { 0 }, wlock { } { }

        rwlock_base(const rwlock_base &) = delete;
        rwlock_base(rwlock_base &&) = delete;

        rwlock_base &operator=(const rwlock_base &) = delete;
        rwlock_base &operator=(rwlock_base &&) = delete;

        void read_lock()
        {
            acquire();
            while (true)
            {
                auto val = state.load(std::memory_order_acquire);
                if (val & writer_mask)
                {
                    lock::pause();
                    continue;
                }

                if (state.compare_exchange_weak(
                        val, val + reader_unit, std::memory_order_acquire, std::memory_order_relaxed
                    ))
                    return;
            }
        }

        void read_unlock()
        {
            bug_on((state.load(std::memory_order_relaxed) & ~writer_mask) == 0);
            state.fetch_sub(reader_unit, std::memory_order_release);
            release();
        }

        void write_lock()
        {
            acquire();
            wlock.lock();

            state.fetch_or(writer_wait, std::memory_order_relaxed);
            while (state.load(std::memory_order_acquire) & ~writer_mask)
                lock::pause();

            state.store(writer_held, std::memory_order_release);
        }

        void write_unlock()
        {
            bug_on(!(state.load(std::memory_order_relaxed) & writer_held));
            state.store(0, std::memory_order_release);
            wlock.unlock();
            release();
        }

        void downgrade()
        {
            bug_on(!(state.load(std::memory_order_relaxed) & writer_held));
            state.store(reader_unit, std::memory_order_release);
            wlock.unlock();
        }

        [[nodiscard]] bool try_upgrade()
        {
            if (!wlock.try_lock())
                return false;

            state.fetch_or(writer_wait, std::memory_order_relaxed);
            const auto expected = reader_unit | writer_wait;
            if (state.compare_exchange_strong(
                    expected, writer_held, std::memory_order_acq_rel, std::memory_order_acquire
                ))
                return true;

            state.fetch_and(~writer_wait, std::memory_order_release);
            wlock.unlock();
            return false;
        }

        [[nodiscard]] bool is_write_locked() const
        {
            return state.load(std::memory_order_relaxed) & writer_held;
        }

        [[nodiscard]] bool is_read_locked() const
        {
            return (state.load(std::memory_order_relaxed) & ~writer_mask) != 0;
        }

        [[nodiscard]] bool is_locked() const
        {
            const auto val = state.load(std::memory_order_relaxed);
            return (val & writer_held) || (val & ~writer_mask);
        }
    };

    using spinlock = spinlock_base<lock_type::preempt>;
    using spinlock_irq = spinlock_base<lock_type::irq>;

    using rwspinlock = rwlock_base<lock_type::preempt>;
    using rwspinlock_irq = rwlock_base<lock_type::irq>;
} // export namespace lib
