// Copyright (C) 2024-2025  ilobilo

export module lib:spinlock;

import cppstd;

namespace lib::lock
{
    bool lock();
    void unlock(bool ints);
    void pause();

    // auto clock() -> std::uint64_t (*)();
    std::uint64_t (*clock())();
} // namespace lib::lock

export namespace lib
{
    template<bool ints>
    class spinlock { };

    template<>
    class spinlock<false>
    {
        friend class spinlock<true>;

        private:
        std::atomic_size_t _next_ticket;
        std::atomic_size_t _serving_ticket;

        public:
        constexpr spinlock()
            : _next_ticket { 0 }, _serving_ticket { 0 } { }

        spinlock(const spinlock &) = delete;
        spinlock(spinlock &&) = delete;

        spinlock &operator=(const spinlock &) = delete;
        spinlock &operator=(spinlock &&) = delete;

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
            auto clock = lock::clock();
            if (clock == nullptr)
                return try_lock();

            auto target = clock() + ns;
            while (is_locked() && clock() < target)
                lock::pause();

            return try_lock();
        }
    };

    template<>
    class spinlock<true> : public spinlock<false>
    {
        private:
        bool _interrupts;

        public:
        constexpr spinlock()
            : spinlock<false> { }, _interrupts { false } { }

        using spinlock<false>::spinlock;

        void lock()
        {
            spinlock<false>::lock();
            _interrupts = lock::lock();
        }

        bool unlock()
        {
            if (!spinlock<false>::unlock())
                return false;

            lock::unlock(_interrupts);
            return true;
        }
    };
} // export namespace lib