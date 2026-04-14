// Copyright (C) 2022-2024  ilobilo

module x86_64.system.cpu;

import system.cpu.self;

namespace cpu::smap
{
    guard::guard()
    {
        if (supported)
            disable();
    }

    guard::~guard()
    {
        if (supported)
            enable();
    }
} // namespace cpu::smap