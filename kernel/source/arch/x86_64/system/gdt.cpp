// Copyright (C) 2022-2024  ilobilo

module x86_64.system.gdt;

import system.cpu.self;
import system.memory;
import magic_enum;
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

    cpu_local<entries> gdt_local;
    cpu_local<tss::reg> tss_local;

    cpu_local_init(gdt_local);
    cpu_local_init(tss_local);

    namespace tss
    {
        reg &self() { return tss_local.get(); }
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
            log::info("gdt: loading on bsp");

        tss_local->ist[0] = lib::alloc<std::uintptr_t>(boot::kstack_size); // page fault
        tss_local->ist[1] = lib::alloc<std::uintptr_t>(boot::kstack_size); // scheduler

        tss_local->iopboffset = sizeof(tss::reg);

        const auto base = reinterpret_cast<std::uintptr_t>(&tss_local.get());
        const std::uint16_t limit = sizeof(tss::reg) - 1;

        gdt_local = default_gdt;
        gdt_local->tss.limit0 = limit;
        gdt_local->tss.base0 = base;
        gdt_local->tss.base1 = base >> 16;
        gdt_local->tss.base2 = base >> 24;
        gdt_local->tss.base3 = base >> 32;

        const reg gdtr {
            sizeof(entries) - 1,
            reinterpret_cast<std::uintptr_t>(&gdt_local.get()),
        };

        load(reinterpret_cast<std::uintptr_t>(&gdtr), segment::data, segment::code, segment::tss);
    }
} // namespace x86_64::gdt