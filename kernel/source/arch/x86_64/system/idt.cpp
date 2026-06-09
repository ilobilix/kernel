// Copyright (C) 2024-2026  ilobilo

module;

#include <uacpi/acpi.h>

module x86_64.system.idt;

import x86_64.system.syscall;
import x86_64.system.ioapic;
import x86_64.system.lapic;
import x86_64.system.pic;
import system.memory.virt;
import system.random;
import system.sched;
import system.acpi;
import system.cpu;
import frigg;
import arch;

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

            const sched::siginfo_t info {
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

        lib::expect<std::size_t> reserve_one(std::size_t cpu_idx, std::size_t hint)
        {
            if (hint >= num_ints)
                return std::unexpected { lib::err::invalid_argument };

            if (hint < irq(0))
                hint += irq(0);

            if ((!acpi::madt::hdr || (acpi::madt::hdr->flags & ACPI_PIC_ENABLED)) &&
                hint >= irq(0) && hint <= irq(15))
            {
                if (auto ret = handler_at(cpu_idx, hint); ret && !ret->used())
                {
                    ret->reserve();
                    return hint;
                }
            }

            for (std::size_t i = hint; i < num_ints; i++)
            {
                auto handler = handler_at(cpu_idx, i);
                if (!handler)
                    continue;

                if (!handler->used() && !handler->is_reserved())
                {
                    handler->reserve();
                    return i;
                }
            }
            return std::unexpected { lib::err::no_space_left };
        }

        lib::expect<std::size_t> reserve_num(std::size_t cpu_idx, std::size_t count)
        {
            count = std::bit_ceil(count);
            auto base = lib::align_up(irq(0), count);

            while (base + count <= num_ints)
            {
                bool all_free = true;
                for (std::size_t i = 0; i < count; i++)
                {
                    const auto handler = handler_at(cpu_idx, base + i);
                    if (!handler || handler->used() || handler->is_reserved())
                    {
                        all_free = false;
                        break;
                    }
                }

                if (all_free)
                {
                    for (std::size_t i = 0; i < count; i++)
                        handler_at(cpu_idx, base + i)->reserve();
                    return base;
                }
                base += count;
            }
            return std::unexpected { lib::err::no_space_left };
        }
    } // namespace

    using irq_vec = frg::small_vector<
        slot, num_preints,
        frg::allocator<slot>
    >;
    cpu_local(irq_vec, irq_handlers);

    std::array<entry, num_ints> &table() { return idt; }

    [[nodiscard]]
    std::optional<slot &> handler_at(std::size_t cpuidx, std::uint8_t num)
    {
        if (num < irq(0))
            return std::nullopt;

        num -= irq(0);

        auto &handlers = irq_handlers.unsafe_get(cpu::local::nth_base(cpuidx));
        if (num >= handlers.size())
            handlers.resize(std::max(num_ints, static_cast<std::size_t>(num) + 5));

        return handlers[num];
    }

    lib::expect<void> vector_domain::alloc(
        std::span<irq::irq_data *> data,
        const irq::fwspec &spec
    )
    {
        if (data.empty())
            return { };

        const auto cpu_idx = (spec.param_count > vector_domain::param_cpu)
            ? spec.params[vector_domain::param_cpu]
            : cpu::bsp_idx();

        if (data.size() == 1)
        {
            const auto hint = (spec.param_count > vector_domain::param_hint)
                ? spec.params[vector_domain::param_hint]
                : 0;

            auto vec = reserve_one(cpu_idx, hint);
            if (!vec)
                return std::unexpected { vec.error() };

            data[0]->hwirq = *vec;
            data[0]->aux = cpu_idx;
            data[0]->parent = nullptr;
            return { };
        }

        auto base = reserve_num(cpu_idx, data.size());
        if (!base)
            return std::unexpected { base.error() };

        for (std::size_t i = 0; i < data.size(); i++)
        {
            data[i]->hwirq = *base + i;
            data[i]->aux = cpu_idx;
            data[i]->parent = nullptr;
        }
        return { };
    }

    void vector_domain::free(std::span<irq::irq_data *> data)
    {
        for (auto entry : data)
        {
            if (auto handler = idt::handler_at(entry->aux, entry->hwirq))
                handler->reset_all();
        }
    }

    void vector_domain::attach(irq::irq_data &data, irq::handler_fn *fn)
    {
        auto handler = idt::handler_at(data.aux, data.hwirq);
        if (!handler)
            return;

        handler->set([fn](cpu::registers *regs) { if (*fn) (*fn)(regs); });
    }

    void vector_domain::detach(irq::irq_data &data)
    {
        if (auto handler = idt::handler_at(data.aux, data.hwirq))
            handler->reset();
    }

    lib::expect<void> vector_domain::set_affinity(
        irq::irq_data &data, const lib::bitmap &cpus, bool force
    )
    {
        lib::unused(force);

        std::size_t target = -1;
        for (std::size_t i = 0; i < cpus.length(); i++)
        {
            if (!cpus.get(i))
                continue;

            target = i;
            break;
        }
        if (target == -1zu)
            return std::unexpected { lib::err::invalid_argument };

        const auto old_cpu = data.aux;
        const auto old_vec = data.hwirq;
        if (target == old_cpu)
            return { };

        auto nv = reserve_one(target, 0);
        if (!nv)
            return std::unexpected { nv.error() };

        auto old_slot = handler_at(old_cpu, old_vec);
        auto new_slot = handler_at(target, *nv);
        if (!old_slot || !new_slot)
        {
            if (new_slot)
                new_slot->reset_all();
            return std::unexpected { lib::err::invalid_argument };
        }

        new_slot->set(std::move(old_slot->handler));
        new_slot->set_flow(old_slot->flow, old_slot->flow_data);

        old_slot->reset_all();

        data.hwirq = *nv;
        data.aux = target;
        return { };
    }

    lib::expect<irq::msi_msg> vector_domain::compose_msi(irq::irq_data &data)
    {
        const auto vec = static_cast<std::uint8_t>(data.hwirq);
        const auto cpu_idx = data.aux;
        const auto aid = cpu::local::nth(cpu_idx)->arch_id;
        if (aid > 0xFE)
            return std::unexpected { lib::err::not_supported };

        return irq::msi_msg {
            .address = 0xFEE00000ull | (static_cast<std::uint64_t>(aid) << 12),
            .data = static_cast<std::uint32_t>(vec)
        };
    }

    vector_domain *get_vector_domain()
    {
        static vector_domain inst { };
        return &inst;
    }

    extern "C" void *isr_table[];
    extern "C" void isr_handler(cpu::registers *regs)
    {
        const auto vector = regs->vector;
        if (early) [[unlikely]]
        {
            if (vector < irq(0))
                lib::panic(regs, "exception {}: '{}'", vector, exception_messages[vector]);
            else
                lib::panic(regs, "unknown interrupt {}", vector);
            std::unreachable();
        }

        auto &self = cpu::self().unsafe_get();
        self.in_interrupt.store(true, std::memory_order_relaxed);
        std::atomic_signal_fence(std::memory_order_acquire);

        if (vector >= irq(0) && vector <= 0xFF)
        {
            random::add_irq_jitter(vector, regs->rip);

            const auto idx = vector - irq(0);
            auto &irqh = irq_handlers.unsafe_get();
            if (irqh.size() > idx)
            {
                auto &slot = irqh[idx];
                if (slot.flow) [[unlikely]]
                    slot.flow(regs, slot);
                else if (slot.used()) [[likely]]
                {
                    apic::eoi();
                    slot(regs);
                }
                else lib::panic(regs, "unhandled irq {}", vector);
            }
            else apic::eoi();
        }
        else if (vector < irq(0))
        {
            if (vector == 2)
            {
                lib::check_if_panicking(regs);

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

            lib::panic(
                regs, "exception {}: '{}' on cpu {}",
                vector, exception_messages[vector], self.idx
            );
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

    lib::initgraph::task msi_parent_task
    {
        "x86_64.idt.msi-parent",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { arch::bsp_initialised_stage() },
        [] {
            irq::set_msi_parent(get_vector_domain());
        }
    };
} // namespace x86_64::idt
