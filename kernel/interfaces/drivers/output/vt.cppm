// Copyright (C) 2024-2026  ilobilo

export module drivers.output.vt;

import lib;
import std;

export namespace output::vt
{
    constexpr std::size_t num_vts = 8;

    std::size_t active();
    lib::expect<void> activate(std::size_t index);

    //! TODO: TEMPORARY
    bool receive_input(std::span<std::byte> buffer);

    lib::initgraph::stage *registered_stage();
} // export namespace output::vt
