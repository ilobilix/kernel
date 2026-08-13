// Copyright (C) 2024-2026  ilobilo

export module drivers.output.vt;

import lib;
import std;

export namespace output::vt
{
    std::size_t active();
    lib::expect<void> activate(std::size_t index);

    bool receive_input(std::span<std::byte> buffer);
    bool is_decckm();

    lib::initgraph::stage *registered_stage();
} // export namespace output::vt
