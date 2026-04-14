// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/resources.h>
// #include <uacpi/tables.h>
#include <uacpi/utilities.h>
// #include <uacpi/namespace.h>

module system.pci;

import system.memory.virt;
import system.acpi;
import lib;
import std;

namespace pci::acpi
{
    class ecam : public configio
    {
        private:
        std::uintptr_t _base;
        std::uint16_t _seg;
        std::uint8_t _bus_start;
        std::uint8_t _bus_end;

        lib::map::flat_hash<std::uintptr_t, std::uintptr_t> mappings;

        std::uintptr_t getaddr(std::uint32_t bus, std::uint32_t dev, std::uint32_t func, std::size_t offset)
        {
            lib::ensure(_bus_start <= bus && bus <= _bus_end);
            const auto paddr = (_base + ((bus - _bus_start) << 20) | (dev << 15) | (func << 12));

            if (mappings.contains(paddr))
                return mappings[paddr] + offset;

            static constexpr auto size = 1zu << 20;
            const auto vaddr = vmm::alloc_vpages(vmm::space_type::other, size);

            if (!vmm::kernel_pagemap->map(vaddr, paddr, size, vmm::flag::rw, vmm::pagemap::max_page_size(size), vmm::caching::mmio))
                lib::panic("could not map ecam memory");

            mappings[paddr] = vaddr;
            return vaddr + offset;
        }

        public:
        ecam(std::uintptr_t base, std::uint16_t seg, std::uint8_t bus_start, std::uint8_t bus_end)
            : _base { base }, _seg { seg }, _bus_start { bus_start }, _bus_end { bus_end } { }

        std::uint32_t read(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::size_t width) override
        {
            lib::ensure(seg == _seg);
            const auto addr = getaddr(bus, dev, func, offset);
            switch (width)
            {
                case sizeof(std::uint8_t):
                    return lib::mmio::in<8>(addr);
                case sizeof(std::uint16_t):
                    return lib::mmio::in<16>(addr);
                case sizeof(std::uint32_t):
                    return lib::mmio::in<32>(addr);
                default:
                    lib::panic("pci::ecam::read: invalid width {}", width);
            }
            std::unreachable();
        }

        void write(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::uint32_t value, std::size_t width) override
        {
            lib::ensure(seg == _seg);
            auto addr = getaddr(bus, dev, func, offset);
            switch (width)
            {
                case sizeof(std::uint8_t):
                    lib::mmio::out<8>(addr, value);
                    break;
                case sizeof(std::uint16_t):
                    lib::mmio::out<16>(addr, value);
                    break;
                case sizeof(std::uint32_t):
                    lib::mmio::out<32>(addr, value);
                    break;
                default:
                    lib::panic("pci::ecam::write: invalid width {}", width);
            }
        }
    };

    class router : public pci::router
    {
        private:
        uacpi_namespace_node *node;

        public:
        router(std::shared_ptr<pci::router> parent, std::shared_ptr<bus> bus, uacpi_namespace_node *node)
            : pci::router { parent, bus }, node { node }
        {
            if (node == nullptr)
            {
                for (std::size_t i = 0; i < 4; i++)
                    bridge_irq[i] = parent->resolve(bus->associated_bridge.lock()->dev, i + 1);
                mod = model::expansion;
                return;
            }

            uacpi_pci_routing_table *routes;
            auto ret = uacpi_get_pci_routing_table(node, &routes);
            if (ret == UACPI_STATUS_NOT_FOUND)
            {
                if (parent)
                {
                    log::warn("pci: no '_PRT' for bus. assuming expansion bridge routing");
                    for (std::size_t i = 0; i < 4; i++)
                        bridge_irq[i] = parent->resolve(bus->associated_bridge.lock()->dev, i + 1);
                    mod = model::expansion;
                }
                else log::error("pci: no '_PRT' for bus");
                return;
            }
            else if (ret != UACPI_STATUS_OK)
            {
                log::error("pci: failed to evaluate '_PRT': {}", uacpi_status_to_string(ret));
                return;
            }

            for (std::size_t i = 0; i < routes->num_entries; i++)
            {
                const auto &route = routes->entries[i];

                auto triggering = flags::level;
                auto polarity = flags::low;
                auto gsi = route.index;

                int32_t slot = (route.address >> 16) & 0xFFFF;
                int32_t func = route.address & 0xFFFF;
                if (func == 0xFFFF)
                    func = -1;

                if (route.source)
                {
                    uacpi_resources *res;
                    ret = uacpi_get_current_resources(route.source, &res);
                    lib::ensure(ret == UACPI_STATUS_OK);

                    switch (res->entries[0].type)
                    {
                        case UACPI_RESOURCE_TYPE_IRQ:
                        {
                            const auto &irq = res->entries[0].irq;
                            lib::ensure(irq.num_irqs >= 1);
                            gsi = irq.irqs[0];
                            if (irq.triggering == UACPI_TRIGGERING_EDGE)
                                triggering = flags::edge;
                            if (irq.polarity == UACPI_POLARITY_ACTIVE_HIGH)
                                polarity = flags::high;
                            break;
                        }
                        case UACPI_RESOURCE_TYPE_EXTENDED_IRQ:
                        {
                            const auto &irq = res->entries[0].extended_irq;
                            lib::ensure(irq.num_irqs >= 1);
                            gsi = irq.irqs[0];
                            if (irq.triggering == UACPI_TRIGGERING_EDGE)
                                triggering = flags::edge;
                            if (irq.polarity == UACPI_POLARITY_ACTIVE_HIGH)
                                polarity = flags::high;
                            break;
                        }
                        default:
                            lib::panic("pci: invalid link '_CRS' type");
                            break;
                    }
                    uacpi_free_resources(res);
                }

                table.emplace_back(
                    gsi, slot, func, route.pin + 1,
                    triggering | polarity
                );
            }

            uacpi_free_pci_routing_table(routes);
            mod = model::root;
        }

        std::shared_ptr<pci::router> downstream(std::shared_ptr<bus> &bus) override
        {
            uacpi_namespace_node *dev_handle = nullptr;
            if (node != nullptr)
            {
                const auto bridge = bus->associated_bridge.lock();
                struct devctx
                {
                    std::uint64_t addr;
                    uacpi_namespace_node *out;
                } ctx {
                    .addr = static_cast<std::uint64_t>((bridge->dev << 16) | bridge->func),
                    .out = nullptr
                };

                uacpi_namespace_for_each_child(node,
                    [](uacpi_handle opaque, uacpi_namespace_node *node, std::uint32_t)
                    {
                        auto *ctx = reinterpret_cast<devctx *>(opaque);
                        std::uint64_t addr = 0;

                        auto ret = uacpi_eval_simple_integer(node, "_ADR", &addr);
                        if (ret != UACPI_STATUS_OK && ret != UACPI_STATUS_NOT_FOUND)
                            return UACPI_ITERATION_DECISION_CONTINUE;

                        if (addr == ctx->addr)
                        {
                            ctx->out = node;
                            return UACPI_ITERATION_DECISION_BREAK;
                        }
                        return UACPI_ITERATION_DECISION_CONTINUE;
                    }, nullptr, UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, &ctx
                );
                dev_handle = ctx.out;
            }
            return std::make_shared<router>(nullptr, bus, dev_handle);
        }
    };

    bool register_ios()
    {
        uacpi_table table;
        if (uacpi_table_find_by_signature(ACPI_MCFG_SIGNATURE, &table) != UACPI_STATUS_OK)
        {
            log::warn("pci: mcfg table not found");
            return false;
        }

        const auto mcfg = static_cast<acpi_mcfg *>(table.ptr);
        if (mcfg->hdr.length < sizeof(acpi_mcfg) + sizeof(acpi_mcfg_allocation))
        {
            log::warn("pci: mcfg table has no entries");
            uacpi_table_unref(&table);
            return false;
        }

        log::debug("pci: using ecam");

        const std::size_t entries = ((mcfg->hdr.length) - sizeof(acpi_mcfg)) / sizeof(acpi_mcfg_allocation);
        for (std::size_t i = 0; i < entries; i++)
        {
            auto &entry = mcfg->entries[i];
            auto io = std::make_shared<ecam>(entry.address, entry.segment, entry.start_bus, entry.end_bus);

            for (std::size_t b = entry.start_bus; b <= entry.end_bus; b++)
                addio(io, entry.segment, b);
        }

        uacpi_table_unref(&table);
        return true;
    }

    bool register_rbs()
    {
        static constexpr const char *root_ids[] =
        {
            "PNP0A03", // PCI
            "PNP0A08", // PCIe
            nullptr,
        };

        bool found = false;
        uacpi_find_devices_at(
            uacpi_namespace_get_predefined(UACPI_PREDEFINED_NAMESPACE_SB),
            root_ids, [](void *ptr, uacpi_namespace_node *node, std::uint32_t)
            {
                *static_cast<bool *>(ptr) = true;

                std::uint64_t seg = 0, bus = 0;

                uacpi_eval_simple_integer(node, "_SEG", &seg);
                uacpi_eval_simple_integer(node, "_BBN", &bus);

                auto rbus = std::make_shared<pci::bus>(seg, bus, getio(seg, bus));
                rbus->router = std::make_shared<router>(nullptr, rbus, node);
                addrb(rbus);

                return UACPI_ITERATION_DECISION_CONTINUE;
            }, &found
        );

        if (found)
            log::debug("pci: found root buses with acpi");
        return found;
    }
} // namespace pci::acpi