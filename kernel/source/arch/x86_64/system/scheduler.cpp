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
import std;

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
        x86_64::apic::arm(ms * 1'000'000, 0xFF);
    }

    void finalise(std::shared_ptr<thread> thread, std::uintptr_t ip)
    {
        auto &self = cpu::self()->arch;

        auto &regs = thread->regs;
        regs.rflags = 0x202;
        regs.rip = ip;

        auto pages = lib::div_roundup(self.fpu.size, pmm::page_size);
        thread->fpu = lib::tohh(pmm::alloc<std::byte *>(pages));

        thread->pfstack_top = thread->allocate_kstack();

        if (thread->is_user)
        {
            regs.cs = x86_64::gdt::segment::ucode | 0x03;
            regs.ss = x86_64::gdt::segment::udata | 0x03;

            regs.rsp = thread->ustack_top;

            self.fpu.restore(thread->fpu);

            constexpr std::uint16_t default_fcw = 0b1100111111;
            constexpr std::uint32_t default_mxcsr = 0b1111110000000;

            asm volatile ("fldcw %0" :: "m"(default_fcw) : "memory");
            asm volatile ("ldmxcsr %0" :: "m"(default_mxcsr) : "memory");

            self.fpu.save(thread->fpu);
        }
        else
        {
            regs.cs = x86_64::gdt::segment::code;
            regs.ss = x86_64::gdt::segment::data;

            regs.rsp = thread->ustack_top = thread->kstack_top;
        }
    }

    void deinitialise(std::shared_ptr<thread> thread)
    {
        auto pages = lib::div_roundup(cpu::self()->arch.fpu.size, pmm::page_size);
        pmm::free(lib::fromhh(thread->fpu), pages);
    }

    void save(std::shared_ptr<thread> thread)
    {
        if (thread->is_user)
        {
            thread->gs_base = cpu::gs::read_kernel();
            thread->fs_base = cpu::fs::read();
            cpu::self()->arch.fpu.save(thread->fpu);
        }
    }

    void load(std::shared_ptr<thread> thread)
    {
        auto self = cpu::self();
        self->arch.tss.ist[0] = thread->pfstack_top;

        if (thread->is_user)
        {
            cpu::gs::write_kernel(thread->gs_base);
            cpu::fs::write(thread->fs_base);
            self->arch.fpu.restore(thread->fpu);
        }
    }
} // namespace sched::arch