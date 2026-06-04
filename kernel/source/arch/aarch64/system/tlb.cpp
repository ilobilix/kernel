// Copyright (C) 2024-2026  ilobilo

module system.memory.tlb;

import lib;
import std;

namespace tlb::arch
{
    void install_handler(std::size_t cpu_idx)
    {
        // TODO
        lib::unused(cpu_idx);
    }

    void send_ipi_mask(const lib::bitmap &mask)
    {
        if (mask.empty())
            return;

        // TODO
    }
} // namespace tlb::arch
