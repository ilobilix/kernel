// Copyright (C) 2024-2026  ilobilo

module arch;

import x86_64.drivers.timers.tsc;
import x86_64.system.lapic;
import x86_64.system.idt;
import drivers.timers;
import system;

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
            apic::ipi(apic::shorthand::all_noself, apic::delivery::nmi, 0);
    }

    bool in_interrupt()
    {
        sched::preempt_disable();
        auto ret = cpu::self().unsafe_get().in_interrupt.load(std::memory_order_relaxed);
        sched::preempt_enable();
        return ret;
    }

    std::uint64_t cycle_count()
    {
        return x86_64::timers::tsc::rdtsc();
    }

    std::size_t hardware_random(std::span<std::byte> out)
    {
        static const auto supported = [] {
            cpu::id_res res;
            return cpu::id(1, 0, res) && (res.c & (1 << 30));
        } ();
        if (!supported)
            return 0;

        const auto try_rdrand64 = [](std::uint64_t &val) {
            for (std::size_t i = 0; i < 10; i++)
            {
                unsigned char ok;
                asm volatile ("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
                if (ok)
                    return true;
            }
            return false;
        };

        std::size_t produced = 0;
        while (produced < out.size())
        {
            std::uint64_t val;
            if (!try_rdrand64(val))
                break;

            const auto chunk = std::min(out.size() - produced, sizeof(val));
            std::memcpy(out.data() + produced, &val, chunk);
            produced += chunk;
        }
        return produced;
    }

    // called from panic() only
    void dump_regs(cpu::registers *regs, cpu::extra_regs eregs, lib::log::level lvl)
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
        lib::initgraph::entail { bsp_initialised_stage() },
        [] {
            x86_64::apic::init_bsp();
            cpu::init_bsp();
            x86_64::apic::io::init();
        }
    };

    lib::initgraph::task cpus_task
    {
        "arch.cpus.initialise",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            bsp_initialised_stage(),
            timers::initialised_stage()
        },
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
            cpu::gs::write(addr);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

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
            cpu::gs::write(addr);

            x86_64::gdt::init_on(ptr);
            x86_64::idt::init_on(ptr);

            cpu::features::enable();
            x86_64::syscall::init_cpu();

            x86_64::apic::init_cpu();

            ptr->online = true;
        }
    } // namespace core
} // namespace arch
