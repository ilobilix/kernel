// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.gdt;

import system.cpu;
import std;

export namespace x86_64::gdt
{
    namespace segment
    {
        constexpr std::uint8_t null = 0x00;
        constexpr std::uint8_t code = 0x08;
        constexpr std::uint8_t data = 0x10;
        constexpr std::uint8_t udata = 0x18;
        constexpr std::uint8_t ucode = 0x20;
        constexpr std::uint8_t tss = 0x28;
    }

    struct [[gnu::packed]] ptr
    {
        std::uint16_t size;
        std::uint64_t offset;
    };

    struct [[gnu::packed]] entry
    {
        std::uint16_t limit;
        std::uint16_t base0;
        std::uint8_t base1;
        std::uint8_t access;
        std::uint8_t granularity;
        std::uint8_t base2;
    };

    namespace tss
    {
        struct [[gnu::packed]] ptr
        {
            std::uint32_t reserved0;
            std::uint64_t rsp[3];
            std::uint64_t reserved1;
            std::uint64_t ist[7];
            std::uint64_t reserved2;
            std::uint16_t reserved3;
            std::uint16_t iopboffset;
        };

        struct [[gnu::packed]] entry
        {
            std::uint16_t length;
            std::uint16_t base0;
            std::uint8_t base1;
            std::uint8_t flags1;
            std::uint8_t flags2;
            std::uint8_t base2;
            std::uint32_t base3;
            std::uint32_t reserved;
        };
    } // namespace tss

    struct [[gnu::packed]] entries
    {
        entry null;
        entry kcode;
        entry kdata;
        entry ucode;
        entry udata;
        tss::entry tss;
    };

    void init_on(cpu::processor *cpu);
} // export namespace x86_64::gdt