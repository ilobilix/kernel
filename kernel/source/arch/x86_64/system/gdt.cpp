// Copyright (C) 2024-2026  ilobilo

module x86_64.system.gdt;

import system.cpu.local;
import system.memory;
import boot;
import lib;
import std;

namespace x86_64::gdt
{
    namespace
    {
        extern "C" void load_early(std::uintptr_t gdtr, std::uint8_t data, std::uint8_t code);
        extern "C" void load(std::uintptr_t gdtr, std::uint8_t data, std::uint8_t code, std::uint16_t tss);

        constinit entries default_gdt
        {
            { 0x0000, 0x0000, 0x00, 0b00000000, 0x0, 0b0000, 0x00 }, // null
            { 0x0000, 0x0000, 0x00, 0b10011010, 0x0, 0b0010, 0x00 }, // kernel code
            { 0x0000, 0x0000, 0x00, 0b10010010, 0x0, 0b0010, 0x00 }, // kernel data
            { 0x0000, 0x0000, 0x00, 0b11111010, 0x0, 0b0100, 0x00 }, // user code 32 bit
            { 0x0000, 0x0000, 0x00, 0b11110010, 0x0, 0b0010, 0x00 }, // user data
            { 0x0000, 0x0000, 0x00, 0b11111010, 0x0, 0b0010, 0x00 }, // user code
            { 0, 0, 0, 0b10001001, 0x0, 0x0, 0, 0, 0x00000000 } // tss
        };
    } // namespace

    cpu_local(entries, gdt_local, default_gdt);
    cpu_local(tss::reg, tss_local);

    namespace tss
    {
        reg &self() { return tss_local.unsafe_get(); }
    } // namespace tss

    void init()
    {
        static entries early = default_gdt;
        const reg gdtr {
            sizeof(entries) - 1,
            reinterpret_cast<std::uintptr_t>(&early),
        };
        load_early(reinterpret_cast<std::uintptr_t>(&gdtr), segment::data, segment::code);
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx())
            lib::info("gdt: loading on bsp");

        auto &tlocal = tss_local.unsafe_get();
        // #NMI
        tlocal.ist[0] = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;
        // #DF
        tlocal.ist[1] = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;
        // #MC
        tlocal.ist[2] = lib::alloc<std::uintptr_t>(boot::kstack_size) + boot::kstack_size;

        tlocal.iopboffset = sizeof(tss::reg);

        const auto base = reinterpret_cast<std::uintptr_t>(&tlocal);
        const std::uint16_t limit = sizeof(tss::reg) - 1;

        auto &glocal = gdt_local.unsafe_get();
        glocal.tss.limit0 = limit;
        glocal.tss.base0 = base;
        glocal.tss.base1 = base >> 16;
        glocal.tss.base2 = base >> 24;
        glocal.tss.base3 = base >> 32;

        const reg gdtr {
            sizeof(entries) - 1,
            reinterpret_cast<std::uintptr_t>(&glocal),
        };

        load(reinterpret_cast<std::uintptr_t>(&gdtr), segment::data, segment::code, segment::tss);
    }
} // namespace x86_64::gdt
