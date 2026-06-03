// Copyright (C) 2024-2026  ilobilo

export module x86_64.system.ioapic;

import system.irq;
import magic_enum;
import lib;
import std;

export namespace x86_64::apic::io
{
    enum class delivery : std::uint32_t
    {
        fixed = (0b000 << 8),
        low_priority = (0b001 << 8),
        smi = (0b010 << 8),
        nmi = (0b100 << 8),
        init = (0b101 << 8),
        ext_int = (0b111 << 8)
    };

    enum class flag : std::uint32_t
    {
        masked = (1 << 16),
        level_sensative = (1 << 15),
        active_low = (1 << 13),
        logical = (1 << 11)
    };
    using magic_enum::bitwise_operators::operator|;

    struct ioapic_domain : irq::domain
    {
        enum fwparam : std::uint32_t
        {
            param_gsi = 0,
            param_trigger = 1,
            param_cpu = 2,
            param_count = 3
        };

        ioapic_domain();

        lib::expect<void> alloc(
            std::span<irq::irq_data> data, const irq::fwspec &spec
        ) override;

        void free(std::span<irq::irq_data> data) override;

        void attach(irq::irq_data &data, irq::handler_fn *fn) override;
        void detach(irq::irq_data &data) override;

        void mask(irq::irq_data &data) override;
        void unmask(irq::irq_data &data) override;
    };

    ioapic_domain *get_ioapic_domain();

    lib::expect<irq::handle_t> request_gsi(
        std::uint32_t gsi, irq::trigger trig, std::size_t cpu_idx,
        irq::handler_fn fn, std::string_view name = { }
    );

    bool is_initialised();

    void init();
} // export namespace x86_64::apic::io
