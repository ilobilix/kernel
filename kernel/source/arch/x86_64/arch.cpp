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
        else
        {
            while (true)
                asm volatile ("cli; hlt");
        }
        std::unreachable();
    }

    void halt_others()
    {
        using namespace x86_64;
        if (cpu::count() > 1 && apic::is_initialised())
            apic::ipi(apic::shorthand::all_noself, apic::delivery::fixed, idt::panic_int);
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

    bool in_interrupt()
    {
        return cpu::local::available() &&
            cpu::self()->in_interrupt.load(std::memory_order_acquire);
    }

    void dump_regs(cpu::registers *regs, cpu::extra_regs eregs, lib::log_level lvl)
    {
        lib::println(lvl, "cpu context:");
        lib::println(lvl, " - r15: 0x{:016X}, r14: 0x{:016X}", regs->r15, regs->r14);
        lib::println(lvl, " - r13: 0x{:016X}, r12: 0x{:016X}", regs->r13, regs->r12);
        lib::println(lvl, " - r11: 0x{:016X}, r10: 0x{:016X}", regs->r11, regs->r10);
        lib::println(lvl, " - r9:  0x{:016X}, r8:  0x{:016X}", regs->r9, regs->r8);
        lib::println(lvl, " - rbp: 0x{:016X}, rdi: 0x{:016X}", regs->rbp, regs->rdi);
        lib::println(lvl, " - rsi: 0x{:016X}, rdx: 0x{:016X}", regs->rsi, regs->rdx);
        lib::println(lvl, " - rcx: 0x{:016X}, rbx: 0x{:016X}", regs->rcx, regs->rbx);
        lib::println(lvl, " - rax: 0x{:016X}, rsp: 0x{:016X}", regs->rax, regs->rsp);
        lib::println(lvl, " - rip: 0x{0:016X}, err: 0x{1:X} : 0b{1:b}", regs->rip, regs->error_code);
        lib::println(lvl, " - rflags: 0x{:X}, cs: 0x{:X}, ss: 0x{:X}", regs->rflags, regs->cs, regs->ss);
        lib::println(lvl, " - cr2: 0x{:X}, cr3: 0x{:X}, cr4: 0x{:X}", eregs.cr2, eregs.cr3, eregs.cr4);
    }

    void early_init()
    {
        x86_64::gdt::init();
        x86_64::idt::init();
    }

    lib::initgraph::task bsp_task
    {
        "arch.bsp.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { acpi::tables_stage() },
        lib::initgraph::entail { bsp_stage() },
        [] {
            x86_64::apic::init_cpu();

            cpu::init_bsp();
            x86_64::pic::init();
            x86_64::apic::io::init();
        }
    };

    lib::initgraph::task cpus_task
    {
        "arch.cpus.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { bsp_stage(), timers::initialised_stage() },
        lib::initgraph::entail { cpus_stage() },
        [] {
            x86_64::apic::calibrate_timer();
            cpu::init();
            x86_64::timers::tsc::finalise();
        }
    };

    namespace core
    {
        void entry(std::uintptr_t addr)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(addr);

            cpu::gs::write_user(addr);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::gs::write_user(addr);
            cpu::features::enable();

            x86_64::syscall::init_cpu();

            x86_64::timers::kvm::init_cpu();
            x86_64::timers::tsc::init_cpu();

            x86_64::apic::init_cpu();

            ptr->online = true;

            sched::start();
        }

        void bsp(std::uintptr_t addr)
        {
            auto ptr = reinterpret_cast<cpu::processor *>(addr);
            cpu::gs::write_user(addr);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::gs::write_user(addr);
            cpu::features::enable();
            x86_64::syscall::init_cpu();

            ptr->online = true;
        }
    } // namespace core
} // namespace arch