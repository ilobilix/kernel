// Copyright (C) 2024  ilobilo

export module x86_64.system.cpu.self;

import x86_64.system.lapic;
import x86_64.system.gdt;
import x86_64.system.idt;
import system.interrupts;
import frigg;
import lib;
import std;

export namespace cpu::arch
{
    struct processor
    {
        x86_64::idt::entry idt[x86_64::idt::num_ints];
        x86_64::idt::ptr idtr;

        frg::small_vector<
            interrupts::handler, x86_64::idt::num_preints,
            frg::allocator<interrupts::handler>
        > int_handlers;

        x86_64::gdt::entries gdt;
        x86_64::gdt::tss::ptr tss;

        lib::lazy_init<x86_64::apic::lapic> lapic;

        struct {
            void *pvclock = nullptr;
        } kvm;
        struct {
            std::uint64_t n, p;
            std::int64_t offset = 0;
            bool calibrated = false;
        } tsc;
    };
} // export namespace cpu::::arch