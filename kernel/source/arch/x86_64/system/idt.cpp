// Copyright (C) 2024-2026  ilobilo

module x86_64.system.idt;

import x86_64.system.syscall;
import x86_64.system.ioapic;
import x86_64.system.lapic;
import x86_64.system.pic;
import system.memory.virt;
import system.random;
import system.sched;
import system.cpu;
import frigg;
import arch;
import lib;

namespace x86_64::idt
{
    namespace
    {
        constinit std::array<entry, num_ints> idt { };
        constinit reg idtr { };
        constinit bool early = true;

        constexpr std::array<std::string_view, 32> exception_messages
        {
            "division by zero", "debug",
            "non-maskable interrupt",
            "breakpoint", "detected overflow",
            "out-of-bounds", "invalid opcode",
            "no coprocessor", "double fault",
            "coprocessor segment overrun",
            "bad TSS", "segment not present",
            "stack fault", "general protection fault",
            "page fault", "unknown interrupt",
            "coprocessor fault", "alignment check",
            "machine check", "reserved",
            "reserved", "reserved", "reserved",
            "reserved", "reserved", "reserved",
            "reserved", "reserved", "reserved",
            "reserved", "reserved", "reserved"
        };

        void eoi(std::uint8_t vector)
        {
            if (apic::io::is_initialised())
                apic::eoi();
            else
                pic::eoi(vector);
        }

        bool deliver_fault_signal(cpu::registers *regs, std::uintptr_t cr2)
        {
            if (!sched::is_running())
                return false;
            if ((regs->cs & 3) != 3)
                return false;

            const auto thread = sched::current_thread();

            int signo = 0;
            switch (regs->vector)
            {
                case 0: // #DE
                    signo = sched::sigfpe;
                    break;
                case 1: // #DB
                    signo = sched::sigtrap;
                    break;
                case 3: // #BP
                    signo = sched::sigtrap;
                    break;
                case 4: // #OF
                    signo = sched::sigsegv;
                    break;
                case 5: // #BR
                    signo = sched::sigsegv;
                    break;
                case 6: // #UD
                    signo = sched::sigill;
                    break;
                case 7: // #NM
                    signo = sched::sigfpe;
                    break;
                case 13: // #GP
                    signo = sched::sigsegv;
                    break;
                case 14: // #PF
                    signo = sched::sigsegv;
                    break;
                case 16: // #MF
                    signo = sched::sigfpe;
                    break;
                case 17: // #AC
                    signo = sched::sigbus;
                    break;
                case 19: // #XM
                    signo = sched::sigfpe;
                    break;
                default:
                    return false;
            }

            sched::siginfo_t info {
                .signo = signo,
                .code = sched::si_kernel,
                .err = static_cast<int>(regs->error_code),
                .pid = 0,
                .uid = 0,
                .status = 0,
                .addr = (regs->vector == 14) ? cr2 : regs->rip,
                .value = 0
            };
            sched::send_signal(thread, info);
            return true;
        }
    } // namespace

    using irq_vec = frg::small_vector<
        interrupts::handler, x86_64::idt::num_preints,
        frg::allocator<interrupts::handler>
    >;
    cpu_local(irq_vec, irq_handlers);

    std::array<entry, num_ints> &table() { return idt; }

    [[nodiscard]]
    auto handler_at(std::size_t cpuidx, std::uint8_t num) -> std::optional<std::reference_wrapper<interrupts::handler>>
    {
        if (num < irq(0))
            return std::nullopt;

        num -= irq(0);

        auto &handlers = irq_handlers.unsafe_get(cpu::local::nth_base(cpuidx));
        if (num >= handlers.size())
            handlers.resize(std::max(num_ints, static_cast<std::size_t>(num) + 5));

        return handlers[num];
    }

    extern "C" void *isr_table[];
    extern "C" void isr_handler(cpu::registers *regs)
    {
        const auto vector = regs->vector;
        if (early) [[unlikely]]
        {
            lib::panic(regs, "exception {}: '{}'", vector, exception_messages[vector]);
            std::unreachable();
        }

        auto &self = cpu::self().unsafe_get();
        self.in_interrupt.store(true, std::memory_order_relaxed);
        std::atomic_signal_fence(std::memory_order_acquire);

        if (vector >= irq(0) && vector <= 0xFF)
        {
            eoi(vector);
            random::add_irq_jitter(vector, regs->rip);

            const auto idx = vector - irq(0);
            auto &irqh = irq_handlers.unsafe_get();
            if (irqh.size() > idx)
            {
                auto &handler = irqh[idx];
                if (handler.used()) [[likely]]
                    handler(regs);
                else
                    lib::panic(regs, "unhandled irq {}", vector);
            }
        }
        else if (vector < irq(0))
        {
            if (vector == 2)
            {
                lib::check_if_panicking();

                const auto status = lib::io::in<8>(0x61);
                if (status & (1 << 7))
                    lib::panic("nmi: parity check");
                else if (status & (1 << 6))
                    lib::panic("nmi: channel check");

                goto skip_sched;
            }

            std::uintptr_t cr2 = 0;
            if (vector == 14)
            {
                cr2 = cpu::read_reg<"cr2">();
                vmm::pfault_state state {
                    cr2,
                    (regs->error_code & (1 << 0)) != 0,
                    (regs->error_code & (1 << 1)) != 0,
                    (regs->error_code & (1 << 4)) != 0,
                    (regs->error_code & (1 << 2)) != 0
                };
                if (vmm::handle_pfault(state))
                    goto end;

                if ((regs->cs & 3) != 3 && sched::is_running())
                {
                    const auto thread = sched::current_thread();
                    if (thread->fault_frame.pc != 0 &&
                        lib::classify_address(cr2, 1) == lib::address_space::user)
                    {
                        regs->rsp = thread->fault_frame.sp;
                        regs->rip = thread->fault_frame.pc;
                        thread->fault_frame.pc = 0;
                        goto end;
                    }
                }
            }

            if (deliver_fault_signal(regs, cr2))
                goto end;

            if (sched::is_running())
            {
                const auto thread = sched::current_thread();
                lib::panic(regs, "exception {}: '{}' on cpu {} on [{}:{}]",
                    vector, exception_messages[vector],
                    self.idx, thread->proc->pid, thread->tid
                );
            }
            lib::panic(regs, "exception {}: '{}' on cpu {}", vector, exception_messages[vector], self.idx);

            std::unreachable();
        }
        else
        {
            lib::panic(regs, "unknown interrupt {}", vector);
            std::unreachable();
        }

        end:
        if (sched::is_running())
        {
            if (!sched::is_preempt_disabled() && sched::current_thread()->needs_resched())
                sched::schedule();

            if ((regs->cs & 3) == 3)
            {
                sched::die_if_kill_pending();
                sched::handle_pending_signals(regs);
            }
        }

        skip_sched:
        std::atomic_signal_fence(std::memory_order_release);
        self.in_interrupt.store(false, std::memory_order_relaxed);
    }

    void init()
    {
        for (std::size_t i = 0; i < num_ints; i++)
            idt[i].set(isr_table[i]);

        idtr.limit = sizeof(idt) - 1;
        idtr.base = reinterpret_cast<std::uintptr_t>(idt.data());
        idtr.load();
    }

    void init_on(cpu::processor *cpu)
    {
        if (cpu->idx == cpu::bsp_idx())
        {
            lib::info("idt: setting up irq handlers");
            idt[2].ist = 1; idt[8].ist = 2; idt[18].ist = 3;
        }

        irq_handlers.unsafe_get(cpu::local::nth_base(cpu->idx)).resize(num_preints);

        if (cpu->idx == cpu::bsp_idx())
            early = false;

        idtr.load();
    }
} // namespace x86_64::idt
