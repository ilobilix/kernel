// Copyright (C) 2024-2026  ilobilo

export module arch:impl;

import system.cpu.arch;

export namespace arch
{
    void wfi() { asm volatile ("wfi"); }
    void pause() { asm volatile ("isb" : : : "memory"); }

    void int_switch(bool on)
    {
        if (on)
            cpu::msr<"daifclr", "i">(0b1111);
        else
            cpu::msr<"daifset", "i">(0b1111);
    }

    bool int_status() { return cpu::mrs<"daif">() == 0; }
} // export namespace arch
