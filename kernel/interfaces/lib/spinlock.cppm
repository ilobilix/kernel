// Copyright (C) 2024-2026  ilobilo

export module lib:spinlock;

import std;

namespace lib::lock
{
    export void acquire_irq();
    export void release_irq();

    void pause();
} // namespace lib::lock

namespace lib
{
    enum class lock_type
    {
        spin,
        irq,
        preempt
    };
} // namespace lib

export namespace lib
{
    template<lock_type Type>
    class spinlock_base { };

    template<>
    class spinlock_base<lock_type::spin>
    {
        private:
        std::atomic_size_t _next_ticket;
        std::atomic_size_t _serving_ticket;

        public:
        constexpr spinlock_base()
            : _next_ticket { 0 }, _serving_ticket { 0 } { }

        spinlock_base(const spinlock_base &) = delete;
        spinlock_base(spinlock_base &&) = delete;

        spinlock_base &operator=(const spinlock_base &) = delete;
        spinlock_base &operator=(spinlock_base &&) = delete;

        void lock()
        {
            const auto ticket = _next_ticket.fetch_add(1, std::memory_order_relaxed);
            while (_serving_ticket.load(std::memory_order_acquire) != ticket)
                lock::pause();
        }

        bool unlock()
        {
            if (is_locked() == false)
                return false;

            const auto current = _serving_ticket.load(std::memory_order_relaxed);
            _serving_ticket.store(current + 1, std::memory_order_release);
            return true;
        }

        bool is_locked() const
        {
            const auto current = _serving_ticket.load(std::memory_order_relaxed);
            const auto next = _next_ticket.load(std::memory_order_relaxed);
            return current != next;
        }

        bool try_lock()
        {
            if (is_locked())
                return false;

            lock();
            return true;
        }
    };

    template<>
    class spinlock_base<lock_type::irq> : public spinlock_base<lock_type::spin>
    {
        public:
        constexpr spinlock_base()
            : spinlock_base<lock_type::spin> { } { }

        using spinlock_base<lock_type::spin>::spinlock_base;

        void lock()
        {
            lock::acquire_irq();
            spinlock_base<lock_type::spin>::lock();
        }

        bool unlock()
        {
            if (!spinlock_base<lock_type::spin>::unlock())
                return false;

            lock::release_irq();
            return true;
        }
    };

    template<>
    class spinlock_base<lock_type::preempt> : public spinlock_base<lock_type::spin>
    {
        public:
        constexpr spinlock_base()
            : spinlock_base<lock_type::spin> { } { }

        using spinlock_base<lock_type::spin>::spinlock_base;

        void lock();
        bool unlock();
    };

    using spinlock = spinlock_base<lock_type::spin>;
    using spinlock_irq = spinlock_base<lock_type::irq>;
    using spinlock_preempt = spinlock_base<lock_type::preempt>;
} // export namespace lib
