// Copyright (C) 2024-2025  ilobilo

module lib;

import system.time;
import arch;
import std;

namespace lib
{
    template<bool ints>
    void spinlock<ints>::arch_lock()
    {
        if constexpr (ints)
        {
            _interrupts = arch::int_status();
            arch::int_toggle(false);
        }
    }

    template<bool ints>
    void spinlock<ints>::arch_unlock() const
    {
        if constexpr (ints)
        {
            if (arch::int_status() != _interrupts)
                arch::int_toggle(_interrupts);
        }
    }

    template<bool ints>
    void spinlock<ints>::arch_pause() const
    {
        arch::pause();
    }

    template<bool ints>
    bool spinlock<ints>::try_lock_until(std::uint64_t ns)
    {
        auto clock = time::main_clock();
        if (clock == nullptr)
            return try_lock();

        auto target = clock->ns() + ns;
        while (is_locked() && clock->ns() < target)
            arch_pause();

        return try_lock();
    }

    template class spinlock<true>;
    template class spinlock<false>;
} // namespace lib