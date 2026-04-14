// Copyright (C) 2024  ilobilo

export module lib:event;
import std;

export namespace lib
{
    struct simple_event
    {
        private:
        std::atomic_size_t _triggers = 0;
        std::atomic_size_t _awaiters = 0;
        std::mutex _lock;

        void arch_pause();

        public:
        void await();
        bool await_timeout(std::size_t ns);

        void trigger(bool drop = false)
        {
            std::unique_lock _ { _lock };
            if (_awaiters == 0 && drop == true)
                return;

            _triggers.fetch_add(1, std::memory_order_acquire);
        }

        std::size_t drop()
        {
            std::unique_lock _ { _lock };
            if (_awaiters == 0)
                return false;

            const std::size_t dropped = _triggers;
            _triggers.store(0, std::memory_order_release);
            return dropped;
        }

        std::size_t num_awaiters()
        {
            return _awaiters;
        }

        simple_event() = default;
    };
} // export namespace lib