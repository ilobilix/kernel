// Copyright (C) 2024-2026  ilobilo

export module drivers.input:kbd;

import lib;
import std;

import :spec;

export namespace input::kbd
{
    enum class term_mode : std::uint8_t
    {
        app_cursor,
        autorepeat,
        newline
    };

    enum class meta_mode : std::uint8_t
    {
        high_bit = 0x03,
        escape_prefix = 0x04
    };

    std::optional<kbmode> get_mode(std::size_t console);
    bool set_mode(std::size_t console, kbmode mode);

    bool set_term_mode(std::size_t console, term_mode mode, bool enabled);
    bool reset_term_modes(std::size_t console);

    std::optional<meta_mode> get_meta_mode(std::size_t console);
    bool set_meta_mode(std::size_t console, meta_mode mode);

    std::optional<std::uint8_t> get_led_state(std::size_t console);
    bool set_led_state(std::size_t console, std::uint32_t state);
    bool set_led_lights(std::size_t console, std::uint32_t lights);
    void refresh_leds();
} // export namespace input::kbd
