// Copyright (C) 2024-2026  ilobilo

module system.cpu.call;

import x86_64.system.lapic;
import x86_64.system.idt;
import system.cpu.local;

namespace cpu
{
    using namespace x86_64;

    void install_handler(std::size_t cpu_idx)
    {
        auto slot = idt::handler_at(cpu_idx, idt::vec_cpu_call);
        lib::bug_on(!slot.has_value() || slot->used());
        slot->set([](auto) { handle_ipi(); });
    }

    void notify_mask(const lib::bitmap_view mask)
    {
        const auto self_idx = self().unsafe_get().idx;
        for (std::size_t i = 0; i < mask.length(); i++)
        {
            if (i == self_idx || !mask.get(i))
                continue;

            auto proc = local::nth(i);
            if (!proc->online.load(std::memory_order_acquire))
                continue;

            apic::ipi(
                proc->arch_id,
                apic::destination::physical,
                apic::delivery::fixed,
                idt::vec_cpu_call
            );
        }
    }
} // namespace cpu
