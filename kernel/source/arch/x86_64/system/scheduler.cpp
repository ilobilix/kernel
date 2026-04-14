// Copyright (C) 2024-2026  ilobilo

module system.scheduler;

import x86_64.system.lapic;
import x86_64.system.gdt;
import x86_64.system.idt;
import system.interrupts;
import system.memory;
import system.cpu.local;
import system.cpu;
import magic_enum;
import arch;
import boot;
import lib;
import std;

namespace sched
{
    void schedule(cpu::registers *regs);
} // namespace sched

namespace sched::arch
{
    static constexpr std::size_t sched_vector = 0xFE;

    using namespace x86_64;

    void init()
    {
        idt::table()[sched_vector].ist = 2;
        auto [handler, _] = interrupts::allocate(cpu::self()->idx, sched_vector).value();
        handler.set(schedule);
    }

    void reschedule(std::size_t ms)
    {
        if (ms == 0)
            asm volatile ("int %0" :: "i"(sched_vector));
            // apic::ipi(apic::shorthand::self, apic::delivery::fixed, sched_vector);
        else
            apic::arm(ms * 1'000'000, sched_vector);
    }

    void reschedule_other(std::size_t cpu)
    {
        apic::ipi(
            cpu::local::nth(cpu)->arch_id,
            apic::destination::physical,
            apic::delivery::fixed,
            sched_vector
        );
    }

    void finalise(process *proc, thread *thread, std::uintptr_t ip, std::uintptr_t arg)
    {
        lib::unused(proc);

        auto &regs = thread->regs;
        regs.rflags = 0x202;
        regs.rip = ip;
        regs.rdi = arg;

        const auto &fpu = cpu::features::get_fpu();
        thread->fpu = lib::allocz<std::byte *>(fpu.size);
        thread->fpu_size = fpu.size;

        if (thread->is_user)
        {
            regs.cs = gdt::segment::ucode | 0x03;
            regs.ss = gdt::segment::udata | 0x03;

            regs.rsp = thread->ustack_top;

            fpu.restore(thread->fpu);

            constexpr std::uint16_t default_fcw = 0b1100111111;
            constexpr std::uint32_t default_mxcsr = 0b1111110000000;

            asm volatile ("fldcw %0" :: "m"(default_fcw) : "memory");
            asm volatile ("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");

            fpu.save(thread->fpu);
        }
        else
        {
            regs.cs = gdt::segment::code;
            regs.ss = gdt::segment::data;

            regs.rsp = thread->kstack_top;
        }
    }

    void deinitialise(process *proc, thread *thread)
    {
        lib::unused(proc);
        lib::free(thread->fpu);
    }

    void save(thread *thread)
    {
        if (thread->is_user)
        {
            thread->gs_base = cpu::gs::read_kernel();
            thread->fs_base = cpu::fs::read();
            cpu::features::get_fpu().save(thread->fpu);
        }
    }

    void load(thread *thread)
    {
        cpu::gs::write_user(reinterpret_cast<std::uintptr_t>(thread));

        if (thread->is_user)
        {
            auto &tss = gdt::tss::self();
            tss.rsp[0] = thread->kstack_top;

            cpu::gs::write_kernel(thread->gs_base);
            cpu::fs::write(thread->fs_base);
            cpu::features::get_fpu().restore(thread->fpu);
        }
    }

    void update_stack(thread *thread, std::uintptr_t addr)
    {
        thread->regs.rsp = addr;
    }
} // namespace sched::arch