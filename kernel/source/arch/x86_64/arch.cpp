// Copyright (C) 2024-2025  ilobilo

module arch;

import x86_64.system.lapic;
import x86_64.system.idt;
import drivers.timers;
import system;
import lib;
import cppstd;

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
        if (cpu::cpu_count != 0)
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
        log::println(lvl, "cpu context:");
        log::println(lvl, " - r15: 0x{:016X}, r14: 0x{:016X}", regs->r15, regs->r14);
        log::println(lvl, " - r13: 0x{:016X}, r12: 0x{:016X}", regs->r13, regs->r12);
        log::println(lvl, " - r11: 0x{:016X}, r10: 0x{:016X}", regs->r11, regs->r10);
        log::println(lvl, " - r9:  0x{:016X}, r8:  0x{:016X}", regs->r9, regs->r8);
        log::println(lvl, " - rbp: 0x{:016X}, rdi: 0x{:016X}", regs->rbp, regs->rdi);
        log::println(lvl, " - rsi: 0x{:016X}, rdx: 0x{:016X}", regs->rsi, regs->rdx);
        log::println(lvl, " - rcx: 0x{:016X}, rbx: 0x{:016X}", regs->rcx, regs->rbx);
        log::println(lvl, " - rax: 0x{:016X}, rsp: 0x{:016X}", regs->rax, regs->rsp);
        log::println(lvl, " - rip: 0x{0:016X}, err: 0x{1:X} : 0b{1:b}", regs->rip, regs->error_code);
        log::println(lvl, " - rflags: 0x{:X}, cs: 0x{:X}, ss: 0x{:X}", regs->rflags, regs->cs, regs->ss);
        log::println(lvl, " - cr2: 0x{:X}, cr3: 0x{:X}, cr4: 0x{:X}", eregs.cr2, eregs.cr3, eregs.cr4);
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
        void entry(boot::limine_mp_info *cpu)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(cpu->extra_argument);

            cpu::gs::write_user(cpu->extra_argument);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::gs::write_user(cpu->extra_argument);
            cpu::features::enable();

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
            cpu::gs::write_user(cpu->extra_argument);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::gs::write_user(cpu->extra_argument);
            cpu::features::enable();

            x86_64::syscall::init_cpu();

            x86_64::apic::init_cpu();
            ptr->online = true;
        }
    } // namespace core
} // namespace arch