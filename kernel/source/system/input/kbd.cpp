// Copyright (C) 2024-2026  ilobilo

module system.input;

import drivers.output.vt;
import magic_enum;

// TODO

namespace input
{
    namespace vt = output::vt;
    namespace
    {
        struct keyboard_t
        {
            std::atomic<kbmode> mode = k_xlate;
        };
        std::array<keyboard_t, vt::num_consoles> kbds { };

        keyboard_t *get_keyboard(std::size_t console)
        {
            if (console == 0 || console > kbds.size())
                return nullptr;
            return &kbds[console - 1];
        }
    } // namespace

    lib::expect<kbmode> get_kbmode(std::size_t console)
    {
        const auto kbd = get_keyboard(console);
        if (!kbd)
            return std::unexpected { lib::err::invalid_argument };
        return kbd->mode.load(std::memory_order_acquire);
    }

    lib::expect<void> set_kbmode(std::size_t console, kbmode mode)
    {
        if (!magic_enum::enum_contains(mode))
            return std::unexpected { lib::err::invalid_argument };

        const auto kbd = get_keyboard(console);
        if (!kbd)
            return std::unexpected { lib::err::invalid_argument };
        kbd->mode.store(mode, std::memory_order_release);
        return { };
    }
} // namespace input
