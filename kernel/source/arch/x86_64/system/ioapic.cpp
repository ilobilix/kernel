// Copyright (C) 2024-2026  ilobilo

module;

#include <uacpi/acpi.h>

module x86_64.system.ioapic;

import x86_64.system.lapic;
import x86_64.system.pic;
import x86_64.system.idt;
import system.memory;
import system.acpi;
import system.cpu;
import system.cpu.local;

namespace x86_64::apic::io
{
    namespace
    {
        bool initialised = false;

        class ioapic
        {
            private:
            std::uintptr_t _mmio;
            std::uint32_t _gsi_base;
            std::size_t _redirs;

            static constexpr std::uint32_t entry(std::uint32_t idx)
            {
                return 0x10 + (idx * 2);
            }

            std::uint32_t read(std::uint32_t reg) const
            {
                lib::mmio::out<32>(_mmio, reg);
                return lib::mmio::in<32>(_mmio + 0x10);
            }

            void write(std::uint32_t reg, std::uint32_t value) const
            {
                lib::mmio::out<32>(_mmio, reg);
                lib::mmio::out<32>(_mmio + 0x10, value);
            }

            std::uint64_t read_entry(std::uint32_t idx) const
            {
                const auto lo = read(entry(idx));
                const auto hi = read(entry(idx) + 1);
                return (static_cast<std::uint64_t>(hi) << 32) | lo;
            }

            void write_entry(std::uint32_t idx, std::uint64_t value) const
            {
                write(entry(idx), value & 0xFFFFFFFF);
                write(entry(idx) + 1, value >> 32);
            }

            public:
            ioapic(std::uintptr_t mmio, std::uint32_t gsi_base) : _gsi_base { gsi_base }
            {
                const auto psize = vmm::page_size::small;
                const auto npsize = vmm::pagemap::from_page_size(psize);

                _mmio = vmm::alloc_vspace(npsize);
                lib::debug("ioapic: mapping mmio: 0x{:X} -> 0x{:X}", mmio, _mmio);

                const auto flags = vmm::pflag::rwg;
                const auto cache = vmm::caching::mmio;

                if (const auto ret = vmm::kernel_pagemap->map(_mmio, mmio, npsize, flags, psize, cache); !ret)
                    lib::panic("could not map ioapic mmio: {}", lib::error_name(ret.error()));

                _redirs = ((read(0x01) >> 16) & 0xFF) + 1;
                for (std::size_t i = 0; i < _redirs; i++)
                    mask(i);
            }

            void set_idx(std::size_t idx, std::uint8_t vector, std::size_t dest, flag flags, delivery deliv) const
            {
                std::uint64_t entry = 0;
                entry |= vector;
                entry |= (std::to_underlying(deliv) & (0b111 << 8));
                entry |= (std::to_underlying(flags) & ~0x7FF);
                entry |= (dest << 56);
                write_entry(idx, entry);
            }

            void mask(std::size_t idx) const
            {
                auto entry = read_entry(idx);
                entry |= (1 << 16);
                write_entry(idx, entry);
            }

            void unmask(std::size_t idx) const
            {
                auto entry = read_entry(idx);
                entry &= ~(1 << 16);
                write_entry(idx, entry);
            }

            std::pair<std::uint32_t, std::uint32_t> gsi_range() const
            {
                return { _gsi_base, _gsi_base + _redirs };
            }
        };
        std::vector<ioapic> ioapics;

        const ioapic &gsi2ioapic(std::uint32_t gsi)
        {
            for (const auto &entry : ioapics)
            {
                auto [start, end] = entry.gsi_range();
                if (start <= gsi && gsi <= end)
                    return entry;
            }
            lib::panic("ioapic: ioapic for gsi {} not found", gsi);
            std::unreachable();
        }

        void set_gsi(
            std::size_t gsi, std::uint8_t vector,
            std::size_t dest,flag flags, delivery deliv
        )
        {
            lib::debug("ioapic: redirecting gsi {} to vector 0x{:X}", gsi, vector);
            const auto &entry = gsi2ioapic(gsi);
            entry.set_idx(gsi - entry.gsi_range().first, vector, dest, flags, deliv);
        }

        void mask_gsi(std::uint32_t gsi)
        {
            // lib::debug("ioapic: masking gsi {}", gsi);
            const auto &entry = gsi2ioapic(gsi);
            entry.mask(gsi - entry.gsi_range().first);
        }

        void unmask_gsi(std::uint32_t gsi)
        {
            // lib::debug("ioapic: unmasking gsi {}", gsi);
            const auto &entry = gsi2ioapic(gsi);
            entry.unmask(gsi - entry.gsi_range().first);
        }

        flag flags_for(irq::trigger trig)
        {
            auto ret = flag::masked;
            switch (trig)
            {
                case irq::trigger::edge_falling:
                    ret = ret | flag::active_low;
                    break;
                case irq::trigger::level_high:
                    ret = ret | flag::level_sensative;
                    break;
                case irq::trigger::level_low:
                    ret = ret | flag::level_sensative | flag::active_low;
                    break;
                case irq::trigger::edge_rising:
                case irq::trigger::edge_both:
                case irq::trigger::none:
                    break;
            }
            return ret;
        }

        void level_flow(cpu::registers *regs, idt::slot &slot)
        {
            const std::uint32_t gsi = slot.flow_data;
            mask_gsi(gsi);
            apic::eoi();
            if (slot.handler)
                slot.handler(regs);
            unmask_gsi(gsi);
        }

        bool is_level(irq::trigger trig)
        {
            return trig == irq::trigger::level_high || trig == irq::trigger::level_low;
        }
    } // namespace

    bool is_initialised() { return initialised; }

    ioapic_domain::ioapic_domain()
        : domain { "x86_64-ioapic", idt::get_vector_domain() } { }

    lib::expect<void> ioapic_domain::alloc(
        std::span<irq::irq_data *> data, const irq::fwspec &spec
    )
    {
        if (data.size() != 1)
            return std::unexpected { lib::err::not_supported };
        if (!is_initialised())
            return std::unexpected { lib::err::no_such_device };
        if (spec.param_count <= param_gsi)
            return std::unexpected { lib::err::invalid_argument };

        const auto gsi = spec.params[param_gsi];
        const auto trig = (spec.param_count > param_trigger)
            ? static_cast<irq::trigger>(spec.params[param_trigger])
            : irq::trigger::edge_rising;

        const auto cpu_idx = (spec.param_count > param_cpu)
            ? spec.params[param_cpu]
            : static_cast<std::uint32_t>(cpu::bsp_idx());

        auto parent_data = std::make_unique<irq::irq_data>();
        parent_data->virq = data[0]->virq;
        parent_data->dom = parent;

        const irq::fwspec pspec {
            .param_count = 2,
            .params = { cpu_idx, gsi + idt::irq(0) }
        };

        auto pd_ptr = parent_data.get();
        if (auto ret = parent->alloc({ &pd_ptr, 1 }, pspec); !ret)
            return std::unexpected { ret.error() };

        const auto core = cpu::local::nth(parent_data->aux);
        if (!core)
        {
            parent->free({ &pd_ptr, 1 });
            return std::unexpected { lib::err::invalid_argument };
        }

        set_gsi(
            gsi, parent_data->hwirq, core->arch_id,
            flags_for(trig), delivery::fixed
        );

        data[0]->hwirq = gsi;
        data[0]->trig = trig;
        data[0]->parent = parent_data.release();
        return { };
    }

    void ioapic_domain::free(std::span<irq::irq_data *> data)
    {
        for (auto entry : data)
        {
            mask_gsi(entry->hwirq);

            std::unique_ptr<irq::irq_data> pd { entry->parent };
            entry->parent = nullptr;

            if (pd)
            {
                auto pd_ptr = pd.get();
                parent->free({ &pd_ptr, 1 });
            }
        }
    }

    void ioapic_domain::attach(irq::irq_data &data, irq::handler_fn *fn)
    {
        if (!data.parent)
            return;

        auto &pd = *data.parent;
        parent->attach(pd, fn);

        if (auto handler = idt::handler_at(pd.aux, pd.hwirq))
        {
            if (is_level(data.trig))
                handler->set_flow(level_flow, data.hwirq);
            else
                handler->clear_flow();
        }
    }

    void ioapic_domain::detach(irq::irq_data &data)
    {
        if (!data.parent)
            return;

        auto &pd = *data.parent;
        if (auto handler = idt::handler_at(pd.aux, pd.hwirq))
            handler->clear_flow();
        parent->detach(*data.parent);
    }

    void ioapic_domain::mask(irq::irq_data &data)
    {
        mask_gsi(data.hwirq);
    }

    void ioapic_domain::unmask(irq::irq_data &data)
    {
        unmask_gsi(data.hwirq);
    }

    lib::expect<void> ioapic_domain::set_affinity(
        irq::irq_data &data, const lib::bitmap &cpus, bool force
    )
    {
        if (!data.parent)
            return std::unexpected { lib::err::invalid_argument };

        const std::uint32_t gsi = data.hwirq;
        mask_gsi(gsi);

        if (auto ret = parent->set_affinity(*data.parent, cpus, force); !ret)
        {
            unmask_gsi(gsi);
            return ret;
        }

        const std::uint8_t vec = data.parent->hwirq;
        const auto aid = cpu::local::nth(data.parent->aux)->arch_id;
        const auto flags = flags_for(data.trig) & ~flag::masked;
        set_gsi(gsi, vec, aid, flags, delivery::fixed);
        return { };
    }

    ioapic_domain *get_ioapic_domain()
    {
        static ioapic_domain inst { };
        return &inst;
    }

    lib::expect<irq::handle_t> request_gsi(
        std::uint32_t gsi, irq::trigger trig, std::size_t cpu_idx,
        irq::handler_fn fn, std::string_view name
    )
    {
        const irq::fwspec spec {
            .param_count = ioapic_domain::param_count,
            .params = {
                [ioapic_domain::param_gsi] = gsi,
                [ioapic_domain::param_trigger] = static_cast<std::uint32_t>(trig),
                [ioapic_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx)
            }
        };
        return irq::alloc_and_request(*get_ioapic_domain(), spec, std::move(fn), name);
    }

    void init()
    {
        lib::info("ioapic: setting up");

        lib::panic_if(
            acpi::madt::hdr == nullptr || acpi::madt::ioapics.empty(),
            "ioapic: no ioapics found"
        );

        // masks it
        pic::init();

        for (const auto &entry : acpi::madt::ioapics)
        {
            ioapics.emplace_back(
                static_cast<std::uintptr_t>(entry.address),
                static_cast<std::uint32_t>(entry.gsi_base)
            );
        }

        if (acpi::madt::hdr->flags & ACPI_PIC_ENABLED)
        {
            for (std::uint8_t i = 0; i < 16; i++)
            {
                if (i == 2)
                    continue;

                bool overridden = false;
                for (const auto &entry : acpi::madt::isos)
                {
                    auto src = static_cast<std::uint8_t>(entry.source);
                    if (src == i)
                    {
                        set_gsi(
                            static_cast<std::uint32_t>(entry.gsi), src + 0x20, cpu::bsp_aid(),
                            static_cast<flag>(entry.flags) | flag::masked, delivery::fixed
                        );
                        overridden = true;
                        break;
                    }
                }

                if (!overridden)
                {
                    set_gsi(
                        i, i + 0x20, cpu::bsp_aid(),
                        flag::masked, delivery::fixed
                    );
                }
            }
        }

        // TODO: nmi

        initialised = true;
        irq::set_gsi_requester(&request_gsi);
    }
} // namespace x86_64::apic::io
