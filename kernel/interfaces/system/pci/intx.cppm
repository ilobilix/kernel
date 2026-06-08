// Copyright (C) 2024-2026  ilobilo

export module system.pci:intx;

import system.irq;
import lib;
import std;

import :core;

export namespace pci::intx
{
    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx, irq::handler_fn fn, std::string_view name = { }
    );
} // export namespace pci::intx
