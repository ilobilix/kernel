// Copyright (C) 2024-2025  ilobilo

export module lib:spinlock;

import cppstd;

namespace lib::lock
{
    bool lock_ints();
    void unlock_ints(bool ints);
    bool lock_preempt();
    void unlock_preempt(bool preempt);
    void pause();

    // auto clock() -> std::uint64_t (*)();
    std::uint64_t (*clock())();
} // namespace lib::lock

export namespace lib
{
    enum lock_type { none, ints, preempt };

    template<lock_type Type>
    class spinlock_base { };

    template<>
    class spinlock_base<lock_type::none>
    {
        friend class spinlock_base<lock_type::ints>;
        friend class spinlock_base<lock_type::preempt>;

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
            auto ticket = _next_ticket.fetch_add(1, std::memory_order_relaxed);
            while (_serving_ticket.load(std::memory_order_acquire) != ticket)
                lock::pause();
        }

        bool unlock()
        {
            if (is_locked() == false)
                return false;

            auto current = _serving_ticket.load(std::memory_order_relaxed);
            _serving_ticket.store(current + 1, std::memory_order_release);

            return true;
        }

        bool is_locked() const
        {
            auto current = _serving_ticket.load(std::memory_order_relaxed);
            auto next = _next_ticket.load(std::memory_order_relaxed);
            return current != next;
        }

        bool try_lock()
        {
            if (is_locked())
                return false;

            lock();
            return true;
        }

        bool try_lock_until(std::uint64_t ns)
        {
            const auto clock = lock::clock();
            if (clock == nullptr)
                return try_lock();

            auto target = clock() + ns;
            while (is_locked() && clock() < target)
                lock::pause();

            return try_lock();
        }
    };

    template<>
    class spinlock_base<lock_type::ints> : public spinlock_base<lock_type::none>
    {
        private:
        bool _interrupts;

        public:
        constexpr spinlock_base()
            : spinlock_base<lock_type::none> { }, _interrupts { false } { }

        using spinlock_base<lock_type::none>::spinlock_base;

        void lock()
        {
            spinlock_base<lock_type::none>::lock();
            _interrupts = lock::lock_ints();
        }

        bool unlock()
        {
            if (!spinlock_base<lock_type::none>::unlock())
                return false;

            lock::unlock_ints(_interrupts);
            return true;
        }
    };

    template<>
    class spinlock_base<lock_type::preempt> : public spinlock_base<lock_type::none>
    {
        private:
        bool _preempt;

        public:
        constexpr spinlock_base()
            : spinlock_base<lock_type::none> { }, _preempt { false } { }

        using spinlock_base<lock_type::none>::spinlock_base;

        void lock()
        {
            spinlock_base<lock_type::none>::lock();
            _preempt = lock::lock_preempt();
        }

        bool unlock()
        {
            if (!spinlock_base<lock_type::none>::unlock())
                return false;

            lock::unlock_preempt(_preempt);
            return true;
        }
    };

    using spinlock = spinlock_base<lock_type::none>;
    using spinlock_ints = spinlock_base<lock_type::ints>;
    using spinlock_preempt = spinlock_base<lock_type::preempt>;
} // export namespace lib