// Copyright (C) 2024-2025  ilobilo

export module system.pci:regs;

import lib;
import std;

export namespace pci
{
    enum class cmd : std::uint16_t
    {
        io_space = (1 << 0),
        mem_space = (1 << 1),
        bus_master = (1 << 2),
        int_dis = (1 << 10)
    };

    enum class reg
    {
        venid = 0x00,
        devid = 0x02,
        cmd = 0x04,
        status = 0x06,
        progif = 0x09,
        subclass = 0x0A,
        class_ = 0x0B,
        header = 0x0E,
        bar0 = 0x10,
        secondary_bus = 0x19,
        subordinate_bus = 0x1A,
        capabilities = 0x34,
        intline = 0x3C,
        intpin = 0x3D
    };
} // export namespace pci