// Copyright (C) 2022-2024  ilobilo

module x86_64.system.cpu;

import system.cpu.self;

namespace cpu::smap
{
    guard::guard()
    {
        can_smap = cpu::self()->arch.can_smap;
        if (can_smap)
            disable();
    }

    guard::~guard()
    {
        if (can_smap)
            enable();
    }
} // namespace cpu::smap