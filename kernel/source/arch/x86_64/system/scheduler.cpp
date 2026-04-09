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
    static constexpr std::size_t sched_vector = 0xFD;

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

    void initialise(process *proc, thread *thrd, std::uintptr_t ip, std::uintptr_t arg, bool cloning)
    {
        lib::unused(proc);

        if (!cloning)
        {
            auto &regs = thrd->regs;
            regs.rip = ip;
            regs.rdi = arg;
            regs.rsp = thrd->kstack_top;

            regs.rflags = 0x202;
            regs.cs = gdt::segment::code;
            regs.ss = gdt::segment::data;
        }

        const auto &fpu = cpu::features::get_fpu();
        thrd->fpu = lib::allocz<std::byte *>(fpu.size);
        thrd->fpu_size = fpu.size;

        if (thrd->is_user)
        {
            if (!cloning)
            {
                fpu.restore(thrd->fpu);

                constexpr std::uint16_t default_fcw = 0b1100111111;
                constexpr std::uint32_t default_mxcsr = 0b1111110000000;

                asm volatile ("fldcw %0" :: "m"(default_fcw) : "memory");
                asm volatile ("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");
            }
            fpu.save(thrd->fpu);
        }
    }

    [[noreturn]]
    void enter_user(thread *thread, std::uintptr_t ip, std::uintptr_t stack)
    {
        ::arch::int_switch(false);
        thread->in_trampoline = false;

        gdt::tss::self().rsp[0] = thread->kstack_top;
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
        cpu::gs::write(reinterpret_cast<std::uintptr_t>(thread));

        if (!thread->in_trampoline && thread->is_user)
        {
            gdt::tss::self().rsp[0] = thread->kstack_top;
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
