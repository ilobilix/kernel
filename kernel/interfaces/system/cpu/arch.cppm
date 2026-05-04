// Copyright (C) 2024-2026  ilobilo

export module system.cpu.arch;

export import :impl;
export import system.cpu.regs;

import std;

export namespace cpu
{
    struct extra_regs;

    namespace tlb
    {
        bool has_asids();

        void flush_page(std::uintptr_t addr);
        void flush_page(std::uintptr_t addr, std::size_t asid);
        void flush_asid(std::size_t asid);
        void flush_all();
    } // namespace tlb

    std::uintptr_t self_addr();
} // export namespace cpu
