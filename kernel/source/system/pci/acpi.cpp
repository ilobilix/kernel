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
import magic_enum;
import lib;
import std;

namespace pci
{
    lib::initgraph::stage *ios_discovered_stage();
} // namespace pci

namespace pci::acpi
{
    [[gnu::weak]] std::uint32_t ecam_read(std::uintptr_t addr, std::size_t width);
    [[gnu::weak]] void ecam_write(std::uintptr_t addr, std::uint32_t val, std::size_t width);

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
            lib::bug_on(bus < _bus_start || _bus_end < bus);
            const auto paddr = (_base + ((bus - _bus_start) << 20) | (dev << 15) | (func << 12));

            if (mappings.contains(paddr))
                return mappings[paddr] + offset;

            static constexpr auto size = 1zu << 20;
            // TODO: random page faults when it's in higher half
            const auto vaddr = lib::fromhh(vmm::alloc_vspace(size));

            const auto flags = vmm::pflag::rw;
            const auto psize = vmm::pagemap::max_page_size(size);
            const auto caching = vmm::caching::mmio;

            if (const auto ret = vmm::kernel_pagemap->map(vaddr, paddr, size, flags, psize, caching); !ret)
                lib::panic("could not map ecam memory: {}", magic_enum::enum_name(ret.error()));

            mappings[paddr] = vaddr;
            return vaddr + offset;
        }

        public:
        ecam(std::uintptr_t base, std::uint16_t seg, std::uint8_t bus_start, std::uint8_t bus_end)
            : _base { base }, _seg { seg }, _bus_start { bus_start }, _bus_end { bus_end } { }

        std::uint32_t read(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::size_t width) override
        {
            lib::bug_on(width != sizeof(std::uint8_t) && width != sizeof(std::uint16_t) && width != sizeof(std::uint32_t));
            lib::bug_on(seg != _seg);

            const auto addr = getaddr(bus, dev, func, offset);

#if defined(__x86_64__)
            return ecam_read(addr, width);
#else
            switch (width)
            {
                case sizeof(std::uint8_t):
                    return lib::mmio::in<8>(addr);
                case sizeof(std::uint16_t):
                    return lib::mmio::in<16>(addr);
                case sizeof(std::uint32_t):
                    return lib::mmio::in<32>(addr);
            }
            std::unreachable();
#endif
        }

        void write(std::uint16_t seg, std::uint8_t bus, std::uint8_t dev, std::uint8_t func, std::size_t offset, std::uint32_t value, std::size_t width) override
        {
            lib::bug_on(width != sizeof(std::uint8_t) && width != sizeof(std::uint16_t) && width != sizeof(std::uint32_t));
            lib::bug_on(seg != _seg);

            const auto addr = getaddr(bus, dev, func, offset);

#if defined(__x86_64__)
            ecam_write(addr, value, width);
#else
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
            }
#endif
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
                    log::warn("pci: no '_PRT' for bus {:04X}:{:02X}. assuming expansion bridge routing", bus->seg, bus->id);
                    for (std::size_t i = 0; i < 4; i++)
                        bridge_irq[i] = parent->resolve(bus->associated_bridge.lock()->dev, i + 1);
                    mod = model::expansion;
                }
                else log::error("pci: no '_PRT' for bus {:04X}:{:02X}. no irq routing possible", bus->seg, bus->id);
                return;
            }
            else if (ret != UACPI_STATUS_OK)
            {
                log::error("pci: failed to evaluate '_PRT' for bus {:04X}:{:02X}: {}", bus->seg, bus->id, uacpi_status_to_string(ret));
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
                    lib::bug_on(ret != UACPI_STATUS_OK);

                    switch (res->entries[0].type)
                    {
                        case UACPI_RESOURCE_TYPE_IRQ:
                        {
                            const auto &irq = res->entries[0].irq;
                            lib::bug_on(irq.num_irqs < 1);
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
                            lib::bug_on(irq.num_irqs < 1);
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

        std::shared_ptr<pci::router> downstream(std::shared_ptr<pci::router> me, std::shared_ptr<bus> &bus) override
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
            return std::make_shared<router>(me, bus, dev_handle);
        }
    };

    lib::initgraph::stage *ios_discovered_stage()
    {
        static lib::initgraph::stage stage
        {
            "pci.acpi.ios-discovered",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *rbs_discovered_stage()
    {
        static lib::initgraph::stage stage
        {
            "pci.acpi.rbs-discovered",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    bool need_arch_ios = true;
    bool need_arch_rbs = true;

    lib::initgraph::task ios_task
    {
        "pci.acpi.discover-ios",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::acpi::tables_stage() },
        lib::initgraph::entail { ios_discovered_stage() },
        [] {
            uacpi_table table;
            if (uacpi_table_find_by_signature(ACPI_MCFG_SIGNATURE, &table) != UACPI_STATUS_OK)
            {
                log::warn("pci: mcfg table not found");
                return;
            }

            const auto mcfg = static_cast<acpi_mcfg *>(table.ptr);
            if (mcfg->hdr.length < sizeof(acpi_mcfg) + sizeof(acpi_mcfg_allocation))
            {
                log::warn("pci: mcfg table has no entries");
                uacpi_table_unref(&table);
                return;
            }

            log::debug("pci: using ecam");

            const std::size_t entries = ((mcfg->hdr.length) - sizeof(acpi_mcfg)) / sizeof(acpi_mcfg_allocation);
            for (std::size_t i = 0; i < entries; i++)
            {
                auto &entry = mcfg->entries[i];
                auto io = std::make_shared<ecam>(
                    static_cast<std::uintptr_t>(entry.address),
                    static_cast<std::uint16_t>(entry.segment),
                    static_cast<std::uint8_t>(entry.start_bus),
                    static_cast<std::uint8_t>(entry.end_bus)
                );

                for (std::size_t b = entry.start_bus; b <= entry.end_bus; b++)
                {
                    addio(io, entry.segment, b);
                    need_arch_ios = false;
                }
            }

            uacpi_table_unref(&table);
        }
    };

    lib::initgraph::task rbs_task
    {
        "pci.acpi.discover-rbs",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ::acpi::initialised_stage(), pci::ios_discovered_stage() },
        lib::initgraph::entail { rbs_discovered_stage() },
        [] {
            static constexpr const char *root_ids[]
            {
                "PNP0A03", // PCI
                "PNP0A08", // PCIe
                nullptr,
            };

            std::size_t num = 0;
            uacpi_find_devices_at(
                uacpi_namespace_get_predefined(UACPI_PREDEFINED_NAMESPACE_SB),
                root_ids, [](void *ptr, uacpi_namespace_node *node, std::uint32_t)
                {
                    auto &num = *static_cast<std::size_t *>(ptr);
                    num++;

                    std::uint64_t seg = 0, bus = 0;

                    uacpi_eval_simple_integer(node, "_SEG", &seg);
                    uacpi_eval_simple_integer(node, "_BBN", &bus);

                    auto rbus = std::make_shared<pci::bus>(seg, bus, getio(seg, bus));
                    rbus->router = std::make_shared<router>(nullptr, rbus, node);
                    addrb(rbus);

                    return UACPI_ITERATION_DECISION_CONTINUE;
                }, &num
            );

            if (num)
            {
                log::debug("pci: found {} root bus{} with acpi", num, num > 1 ? "es" : "");
                need_arch_rbs = false;
            }
        }
    };
} // namespace pci::acpi