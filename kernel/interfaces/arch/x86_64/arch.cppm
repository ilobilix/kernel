// Copyright (C) 2024-2026  ilobilo

export module arch:impl;

import std;

export namespace arch
{
    void wfi() { asm volatile ("hlt"); }
    void pause() { asm volatile ("pause"); }

    void int_switch(bool on)
    {
        if (on)
            asm volatile ("sti");
        else
            asm volatile ("cli");
    }

    bool int_status()
    {
        std::uint64_t rflags = 0;
        asm volatile ("pushfq \n\t"
                     "pop %[rflags]"
                     : [rflags] "=r"(rflags));
        return rflags & (1 << 9);
    }
} // export namespace arch
