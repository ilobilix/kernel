// Copyright (C) 2024  ilobilo

module lib;

import system.time;
import arch;
import std;

namespace lib
{
    void simple_event::await()
    {
        _lock.lock();
        _awaiters++;
        _lock.unlock();

        while (_triggers.load(std::memory_order_acquire) == 0)
            arch::pause();

        std::unique_lock _ { _lock };
        if (--_awaiters == 0)
            _triggers.fetch_sub(1, std::memory_order_release);
    }

    bool simple_event::await_timeout(std::size_t ns)
    {
        auto clock = time::main_clock();
        if (clock == nullptr)
            return false;

        _lock.lock();
        _awaiters++;
        _lock.unlock();

        auto start = clock->ns();
        const auto end = start + ns;

        while (start < end)
        {
            if (_triggers.load(std::memory_order_acquire) > 0)
                break;

            time::stall_ns(1'000);
            start = clock->ns();
        }

        std::unique_lock _ { _lock };
        _awaiters--;

        if (start >= end)
            return false;

        if (_awaiters == 0)
            _triggers.fetch_sub(1, std::memory_order_release);

        return true;
    }
} // namespace lib