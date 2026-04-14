// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import x86_64.system.lapic;
import x86_64.system.gdt;
import system.interrupts;
import system.memory;
import system.cpu.self;
import system.cpu;
import arch;
import lib;
import cppstd;

namespace sched
{
    void schedule(cpu::registers *regs);
} // namespace sched

namespace sched::arch
{
    void init()
    {
        auto [handler, _] = interrupts::allocate(cpu::self()->idx, 0xFF).value();
        handler.set(schedule);
    }

    void reschedule(std::size_t ms)
    {
        if (ms == 0)
            x86_64::apic::ipi(0, x86_64::apic::dest::self, 0xFF);
        else
            x86_64::apic::arm(ms * 1'000'000, 0xFF);
    }

    void finalise(std::shared_ptr<thread> &thread, std::uintptr_t ip)
    {
        auto &fpu = cpu::features::fpu;

        auto &regs = thread->regs;
        regs.rflags = 0x202;
        regs.rip = ip;

        auto pages = lib::div_roundup(fpu.size, pmm::page_size);
        thread->fpu = lib::tohh(pmm::alloc<std::byte *>(pages));

        thread->pfstack_top = thread->allocate_kstack();

        if (thread->is_user)
        {
            regs.cs = x86_64::gdt::segment::ucode | 0x03;
            regs.ss = x86_64::gdt::segment::udata | 0x03;

            regs.rsp = thread->ustack_top = thread->allocate_ustack();

            fpu.restore(thread->fpu);

            constexpr std::uint16_t default_fcw = 0b1100111111;
            constexpr std::uint32_t default_mxcsr = 0b1111110000000;

            asm volatile ("fldcw %0" :: "m"(default_fcw) : "memory");
            asm volatile ("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");

            fpu.save(thread->fpu);
        }
        else
        {
            regs.cs = x86_64::gdt::segment::code;
            regs.ss = x86_64::gdt::segment::data;

            regs.rsp = thread->ustack_top = thread->kstack_top;
        }
    }

    void deinitialise(std::shared_ptr<thread> &thread)
    {
        auto &fpu = cpu::features::fpu;
        auto pages = lib::div_roundup(fpu.size, pmm::page_size);
        pmm::free(lib::fromhh(thread->fpu), pages);
    }

    void save(std::shared_ptr<thread> &thread)
    {
        if (thread->is_user)
        {
            thread->gs_base = cpu::gs::read_kernel();
            thread->fs_base = cpu::fs::read();
            cpu::features::fpu.save(thread->fpu);
        }
    }

    void load(std::shared_ptr<thread> &thread)
    {
        x86_64::gdt::tss::self().ist[0] = thread->pfstack_top;

        if (thread->is_user)
        {
            cpu::gs::write_kernel(thread->gs_base);
            cpu::fs::write(thread->fs_base);
            cpu::features::fpu.restore(thread->fpu);
        }
    }
} // namespace sched::arch