// Copyright (C) 2024-2026  ilobilo

export module drivers.output.vt;

import lib;
import std;

export namespace output::vt
{
    constexpr std::size_t num_consoles = 63;

    enum vtmode
    {
        vt_auto = 0x00,
        vt_process = 0x01
    };

    enum kdmode
    {
        kd_text = 0x00,
        kd_graphics = 0x01
    };

    enum gkbtype : std::uint8_t
    {
        kb_84 = 0x01,
        kb_101 = 0x02,
        kb_other = 0x03
    };

    std::size_t active();
    bool activate(std::size_t index);
    bool activate_previous();

    bool receive_input(std::size_t console, std::span<std::byte> buffer);

    lib::initgraph::stage *registered_stage();
} // export namespace output::vt
