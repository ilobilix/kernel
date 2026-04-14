// Copyright (C) 2024-2025  ilobilo

export module system.cpu.arch;

export import :impl;
import std;

export namespace cpu
{
    struct registers;
    struct extra_regs;

    void invlpg(std::uintptr_t address);
    void invlasid(std::uintptr_t, std::size_t);
    bool has_asids();

    std::uintptr_t self_addr();
} // export namespace cpu