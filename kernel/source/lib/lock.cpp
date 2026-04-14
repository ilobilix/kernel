// Copyright (C) 2024-2025  ilobilo

module lib;
import arch;

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

    template class spinlock<true>;
    template class spinlock<false>;
} // namespace lib