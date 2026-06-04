// Copyright (C) 2024-2026  ilobilo

module system.memory.tlb;

import x86_64.system.lapic;
import x86_64.system.idt;
import system.cpu.local;

namespace tlb
{
    void handle_ipi(cpu::registers *regs);
} // namespace tlb

namespace tlb::arch
{
    using namespace x86_64;

    void install_handler(std::size_t cpu_idx)
    {
        auto slot = idt::handler_at(cpu_idx, idt::vec_tlb_shootdown);
        lib::bug_on(!slot.has_value() || slot->used());
        slot->set(handle_ipi);
    }

    void send_ipi_mask(const lib::bitmap &mask)
    {
        const auto self_idx = cpu::self().unsafe_get().idx;
        for (std::size_t i = 0; i < mask.length(); i++)
        {
            if (i == self_idx || !mask.get(i))
                continue;

            auto proc = cpu::local::nth(i);
            if (!proc->online.load(std::memory_order_acquire))
                continue;

            apic::ipi(
                proc->arch_id,
                apic::destination::physical,
                apic::delivery::fixed,
                idt::vec_tlb_shootdown
            );
        }
    }
} // namespace tlb::arch
