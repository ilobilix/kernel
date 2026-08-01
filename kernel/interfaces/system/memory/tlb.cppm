// Copyright (C) 2024-2026  ilobilo

export module system.memory.tlb;

import system.memory.virt;
import system.cpu.regs;
import lib;
import std;

export namespace tlb
{
    enum class scope : std::uint8_t
    {
        user_range,
        user_full,
        kernel_range,
        kernel_full
    };

    struct request_t
    {
        scope sc;
        std::uintptr_t start;
        std::size_t pages;
        const vmm::pagemap *pmap;
    };

    void shootdown(const request_t &req);
    void local_flush(const request_t &req);
} // export namespace tlb
