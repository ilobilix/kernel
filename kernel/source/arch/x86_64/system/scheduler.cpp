// Copyright (C) 2024-2025  ilobilo

module system.scheduler;

import x86_64.system.lapic;
import x86_64.system.gdt;
import x86_64.system.idt;
import system.interrupts;
import system.memory;
import system.cpu.self;
import system.cpu;
import magic_enum;
import arch;
import lib;
import cppstd;

namespace sched
{
    void schedule(cpu::registers *regs);
} // namespace sched

namespace sched::arch
{
    static constexpr std::size_t sched_vector = 0xFE;

    void init()
    {
        // x86_64::idt::table()[14].ist = 1;
        auto [handler, _] = interrupts::allocate(cpu::self()->idx, sched_vector).value();
        handler.set(schedule);
    }

    void reschedule(std::size_t ms)
    {
        if (ms == 0)
            x86_64::apic::ipi(x86_64::apic::shorthand::self, x86_64::apic::delivery::fixed, sched_vector);
        else
            x86_64::apic::arm(ms * 1'000'000, sched_vector);
    }

    void finalise(process *proc, thread *thread, std::uintptr_t ip)
    {
        auto &regs = thread->regs;
        regs.rflags = 0x202;
        regs.rip = ip;

        const auto &fpu = cpu::features::get_fpu();
        const auto pages = lib::div_roundup(fpu.size, pmm::page_size);
        const auto vfpu = vmm::alloc_vpages(vmm::space_type::other, pages);

        if (const auto ret = proc->vmspace->pmap->map_alloc(vfpu, fpu.size, vmm::pflag::rw, vmm::page_size::small); !ret)
            lib::panic("could not map thread fpu storage: {}", magic_enum::enum_name(ret.error()));

        thread->fpu = reinterpret_cast<std::byte *>(vfpu);
        thread->fpu_size = fpu.size;

        thread->pfstack_top = thread::allocate_kstack(proc);

        if (thread->is_user)
        {
            regs.cs = x86_64::gdt::segment::ucode | 0x03;
            regs.ss = x86_64::gdt::segment::udata | 0x03;

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
            regs.cs = x86_64::gdt::segment::code;
            regs.ss = x86_64::gdt::segment::data;

            regs.rsp = thread->kstack_top;
        }
    }

    void deinitialise(process *proc, thread *thread)
    {
        const auto vaddr = reinterpret_cast<std::uintptr_t>(thread->fpu);
        if (const auto ret = proc->vmspace->pmap->unmap_dealloc(vaddr, thread->fpu_size, vmm::page_size::small); !ret)
            lib::panic("could not unmap thread fpu storage: {}", magic_enum::enum_name(ret.error()));
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
        auto &tss = x86_64::gdt::tss::self();
        tss.ist[0] = thread->pfstack_top;
        tss.rsp[0] = thread->kstack_top;

        cpu::gs::write_user(reinterpret_cast<std::uintptr_t>(thread));

        if (thread->is_user)
        {
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