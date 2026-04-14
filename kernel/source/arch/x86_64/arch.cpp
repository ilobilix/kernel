// Copyright (C) 2024-2025  ilobilo

module arch;

import x86_64.system.lapic;
import x86_64.system.idt;
import drivers.timers;
import system;
import lib;
import std;

namespace arch
{
    [[noreturn]]
    void halt(bool ints)
    {
        if (ints)
        {
            while (true)
                asm volatile ("hlt");
        }
        else asm volatile ("cli; hlt");
        std::unreachable();
    }

    void halt_others()
    {
        if (cpu::processors != nullptr)
            x86_64::apic::ipi(0, x86_64::apic::dest::all_noself, x86_64::idt::panic_int);
    }

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
        asm volatile (
            "pushfq \n\t"
            "pop %[rflags]"
            : [rflags]"=r"(rflags)
        );
        return rflags & (1 << 9);
    }

    void dump_regs(cpu::registers *regs, cpu::extra_regs eregs, log::level lvl)
    {
        log::println(lvl, "CPU context:");
        log::println(lvl, " - R15: 0x{:016X}, R14: 0x{:016X}", regs->r15, regs->r14);
        log::println(lvl, " - R13: 0x{:016X}, R12: 0x{:016X}", regs->r13, regs->r12);
        log::println(lvl, " - R11: 0x{:016X}, R10: 0x{:016X}", regs->r11, regs->r10);
        log::println(lvl, " - R9:  0x{:016X}, R8:  0x{:016X}", regs->r9, regs->r8);
        log::println(lvl, " - RBP: 0x{:016X}, RDI: 0x{:016X}", regs->rbp, regs->rdi);
        log::println(lvl, " - RSI: 0x{:016X}, RDX: 0x{:016X}", regs->rsi, regs->rdx);
        log::println(lvl, " - RCX: 0x{:016X}, RBX: 0x{:016X}", regs->rcx, regs->rbx);
        log::println(lvl, " - RAX: 0x{:016X}, RSP: 0x{:016X}", regs->rax, regs->rsp);
        log::println(lvl, " - RIP: 0x{0:016X}, ERR: 0x{1:X} : 0b{1:b}", regs->rip, regs->error_code);
        log::println(lvl, " - RFLAGS: 0x{:X}, CS: 0x{:X}, SS: 0x{:X}", regs->rflags, regs->cs, regs->ss);
        log::println(lvl, " - CR2: 0x{:X}, CR3: 0x{:X}, CR4: 0x{:X}", eregs.cr2, eregs.cr3, eregs.cr4);
    }

    void init()
    {
        cpu::init_bsp();
        x86_64::pic::init();
        x86_64::apic::io::init();

        timers::init();
        x86_64::apic::calibrate_timer();
        cpu::init();
        x86_64::timers::tsc::finalise();
    }

    namespace core
    {
        extern "C" void arch_core_entry(boot::limine_mp_info *cpu)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(cpu->extra_argument);

            vmm::kernel_pagemap->load();

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::gs::write_user(cpu->extra_argument);

            auto &fpu = ptr->arch.fpu;
            std::tie(fpu.size, fpu.save, fpu.restore) = cpu::features::enable();

            x86_64::syscall::init_cpu();

            x86_64::timers::kvm::init();
            x86_64::timers::tsc::init();

            x86_64::apic::init_cpu();
            ptr->online = true;

            sched::start();
        }

        void bsp(boot::limine_mp_info *cpu)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(cpu->extra_argument);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::gs::write_user(cpu->extra_argument);

            auto &fpu = ptr->arch.fpu;
            std::tie(fpu.size, fpu.save, fpu.restore) = cpu::features::enable();

            x86_64::syscall::init_cpu();

            x86_64::apic::init_cpu();
            ptr->online = true;
        }
    } // namespace core
} // namespace arch