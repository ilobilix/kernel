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

extern "C"
{
    void sched_switch_context(cpu::registers *current, cpu::registers *next);

    [[noreturn]]
    void sched_enter_user(std::uintptr_t ip, std::uintptr_t stack);
} // extern "C"

namespace sched::arch
{
    static constexpr std::size_t sched_vector = 0xFE;

    using namespace x86_64;

    void init()
    {
        idt::table()[sched_vector].ist = 3;
        auto ret = interrupts::allocate(cpu::self().unsafe_get().idx, sched_vector);
        lib::bug_on(!ret.has_value());
        auto [handler, vec] = *ret;
        lib::bug_on(sched_vector != vec);
        handler.set(schedule);
    }

    void enable()
    {
        asm volatile ("dec qword ptr gs:24" ::: "memory");
    }

    void disable()
    {
        asm volatile ("inc qword ptr gs:24" ::: "memory");
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

    void initialise(process *proc, thread *thread, std::uintptr_t ip, std::uintptr_t arg)
    {
        lib::unused(proc);

        auto &regs = thread->regs;
        regs.rflags = 0x202;
        regs.rip = ip;
        regs.rdi = arg;

        const auto &fpu = cpu::features::get_fpu();
        thread->fpu = lib::allocz<std::byte *>(fpu.size);
        thread->fpu_size = fpu.size;

        regs.cs = gdt::segment::code;
        regs.ss = gdt::segment::data;

        regs.rsp = thread->kstack_top;

        if (thread->is_user)
        {
            fpu.restore(thread->fpu);

            constexpr std::uint16_t default_fcw = 0b1100111111;
            constexpr std::uint32_t default_mxcsr = 0b1111110000000;

            asm volatile ("fldcw %0" :: "m"(default_fcw) : "memory");
            asm volatile ("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");

            fpu.save(thread->fpu);
        }
    }

    [[noreturn]]
    void enter_user(thread *thread, std::uintptr_t ip, std::uintptr_t stack)
    {
        lib::unused(thread);
        ::arch::int_switch(false);
        thread->in_trampoline = false;

        auto &tss = gdt::tss::self();
        tss.rsp[0] = thread->kstack_top;
        cpu::gs::write_kernel(thread->gs_base);

        sched_enter_user(ip, stack);
    }

    void deinitialise(process *proc, thread *thread)
    {
        lib::unused(proc);
        lib::free(thread->fpu);
    }

    void save(thread *thread)
    {
        if (!thread->in_trampoline && thread->is_user)
        {
            thread->gs_base = cpu::gs::read_kernel();
            thread->fs_base = cpu::fs::read();
            cpu::features::get_fpu().save(thread->fpu);
        }
    }

    void load(thread *thread)
    {
        cpu::gs::write_user(reinterpret_cast<std::uintptr_t>(thread));

        if (!thread->in_trampoline && thread->is_user)
        {
            auto &tss = gdt::tss::self();
            tss.rsp[0] = thread->kstack_top;

            cpu::gs::write_kernel(thread->gs_base);
            cpu::fs::write(thread->fs_base);
            cpu::features::get_fpu().restore(thread->fpu);
        }
    }

    void ctx_switch(thread *current, thread *next)
    {
        lib::bug_on(!current || !next);
        sched_switch_context(&current->regs, &next->regs);
    }
} // namespace sched::arch
