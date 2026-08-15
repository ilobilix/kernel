// Copyright (C) 2024-2026  ilobilo

export module drivers.input:kbd;

import lib;
import std;

import :spec;

export namespace input::kbd
{
    enum class mode_flag : std::uint8_t
    {
        none = 0,
        app_keypad = (1 << 0),
        app_cursor = (1 << 1),
        autorepeat = (1 << 2),
        newline = (1 << 3),
        meta_escape = (1 << 4)
    };

    std::optional<kbmode> get_mode(std::size_t console);
    bool set_mode(std::size_t console, kbmode mode);

    std::optional<bool> get_mode_flag(std::size_t console, mode_flag flag);
    bool set_mode_flag(std::size_t console, mode_flag flag, bool enabled);
    bool reset_mode_flags(std::size_t console);

    std::optional<std::uint8_t> get_led_state(std::size_t console);
    bool set_led_state(std::size_t console, std::uint32_t state);
    bool set_led_lights(std::size_t console, std::uint32_t lights);
    void refresh_leds();
} // export namespace input::kbd
