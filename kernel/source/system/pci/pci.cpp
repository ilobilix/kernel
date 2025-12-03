// Copyright (C) 2024-2025  ilobilo

module system.pci;

import system.memory;
import system.acpi;
import magic_enum;
import lib;
import std;

namespace pci
{
    namespace
    {
        lib::map::flat_hash<std::uint32_t, std::shared_ptr<configio>> ios;
        std::vector<std::shared_ptr<bus>> rbs;

        lib::map::flat_hash<std::uint32_t, std::shared_ptr<bridge>> brdgs;
        lib::map::flat_hash<std::uint32_t, std::shared_ptr<device>> devs;

        void enum_bus(const auto &bus);

        void enum_func(const auto &bus, std::uint8_t dev, std::uint8_t func)
        {
            const auto venid = bus->template read<16>(dev, func, reg::venid);
            const auto devid = bus->template read<16>(dev, func, reg::devid);
            if (venid == 0xFFFF || devid == 0xFFFF)
                return;

            const auto header = bus->template read<8>(dev, func, reg::header) & 0x7F;
            if (header == 0x00) // device
            {
                // lib::info("pci: {:04X}:{:02X}:{:02X}:{:02X}: general device: {:04X}:{:04X}", bus->seg, bus->id, dev, func, venid, devid);
                lib::info("pci: general device: {:04X}:{:04X}", venid, devid);

                const auto progif = bus->template read<8>(dev, func, reg::progif);
                const auto subclass = bus->template read<8>(dev, func, reg::subclass);
                const auto class_ = bus->template read<8>(dev, func, reg::class_);

                auto device = std::make_shared<pci::device>(bus, dev, func);
                device->venid = venid;
                device->devid = devid;
                device->progif = progif;
                device->subclass = subclass;
                device->class_ = class_;

                const auto pin = device->template read<8>(reg::intpin);
                if (pin != 0 && bus->router)
                    device->irq.route = bus->router->resolve(dev, pin);

                bus->devices.push_back(device);
                devs[devidx(device)] = device;
            }
            else if (header == 0x01) // PCI-to-PCI bridge
            {
                // lib::info("pci: {:04X}:{:02X}:{:02X}:{:02X}: bridge: {:04X}:{:04X}", bus->seg, bus->id, dev, func, venid, devid);
                lib::info("pci: bridge: {:04X}:{:04X}", venid, devid);
                auto bridge = std::make_shared<pci::bridge>(bus, dev, func);

                const auto secondary_id = bridge->template read<8>(reg::secondary_bus);
                if (secondary_id)
                {
                    lib::debug("pci: secondary bus: {:04X}:{:02X}", bus->seg, secondary_id);

                    bridge->secondary_bus = secondary_id;
                    bridge->subordinate_bus = bridge->template read<8>(reg::subordinate_bus);

                    auto secondary_bus = std::make_shared<pci::bus>(bus->seg, secondary_id, bus->io, bridge, nullptr);
                    if (bus->router)
                        secondary_bus->router = bus->router->downstream(bus->router, secondary_bus);

                    bridge->associated_bus = secondary_bus;
                    enum_bus(secondary_bus);
                }

                bus->bridges.push_back(bridge);
                brdgs[devidx(bridge)] = bridge;
            }
            else lib::panic("pci: unknown header type: {:X}", header);
        }

        void enum_dev(const auto &bus, std::uint8_t dev)
        {
            const auto venid = bus->template read<16>(dev, 0, reg::venid);
            if (venid == 0xFFFF)
                return;

            const auto header = bus->template read<8>(dev, 0, reg::header);
            if (header & (1 << 7))
            {
                for (std::uint32_t i = 0; i < 8; i++)
                    enum_func(bus, dev, i);
            }
            else enum_func(bus, dev, 0);
        }

        void enum_bus(const auto &bus)
        {
            bool devs32 = true;
            if (!bus->associated_bridge.expired())
            {
                const auto bridge = bus->associated_bridge.lock();
                if (bridge->is_pcie && bridge->is_secondary)
                    devs32 = false;
            }
            for (std::uint8_t i = 0; i < (devs32 ? 32 : 1); i++)
                enum_dev(bus, i);
        }
    } // namespace

    auto router::resolve(std::int32_t dev, std::uint8_t pin, std::int32_t func) -> entry *
    {
        if (mod == model::root)
        {
            const auto entry = std::ranges::find_if(table, [&](const auto &entry) {
                bool ret = (entry.dev == dev && entry.pin == pin);
                if (func != -1)
                    ret = ret && (entry.func == -1 || entry.func == func);
                return ret;
            });

            if (entry == table.cend())
                return nullptr;

            return std::addressof(*entry);
        }
        else if (mod == model::expansion)
            return bridge_irq[(static_cast<std::size_t>(pin) - 1 + dev) % 4];

        return nullptr;
    }

    std::uintptr_t bar::map()
    {
        lib::bug_on(type != type::mem);
        lib::bug_on(!phys || !size);

        if (virt != 0)
            return virt;

        auto &pmap = vmm::kernel_pagemap;

        const auto psize = vmm::page_size::small;
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto paddr = lib::align_down(phys, npsize);
        const auto alsize = lib::align_up(size + (phys - paddr), npsize);

        const auto vaddr = vmm::alloc_vspace(lib::div_roundup(size, pmm::page_size));

        const auto flags = vmm::pflag::rwg;
        const auto cache = vmm::caching::mmio;

        if (const auto ret = pmap->map(vaddr, paddr, alsize, flags, psize, cache); !ret)
            lib::panic("could not map pci bar: {}", magic_enum::enum_name(ret.error()));

        return virt = (vaddr + (phys - paddr));
    }

    entity::entity(std::weak_ptr<pci::bus> parent, std::uint8_t dev, std::uint8_t func)
        : dev { dev }, func { func }, parent { parent }
    {
        if (const auto status = read<16>(reg::status); status & (1 << 4))
        {
            auto offset = read<16>(reg::capabilities) & 0xFC;
            while (offset)
            {
                const auto entry = read<16>(offset);
                const std::uint8_t type = entry & 0xFF;
                if (type == 0x10)
                {
                    is_pcie = true;
                    auto tp = (read<16>(offset + 2) >> 4) & 0x0F;
                    is_secondary = (tp == 4 || tp == 6 || tp == 8);
                }
                caps.emplace_back(type, offset);
                offset = (entry >> 8) & 0xFC;
            }
        }
    }

    void entity::read_bars(std::size_t nbars)
    {
        auto bars = get_bars();

        for (std::size_t i = 0; i < nbars; i++)
        {
            bar ret { 0, 0, 0, false, false, bar::type::invalid };
            bool bit64 = false;

            auto offset = std::to_underlying(reg::bar0) + i * sizeof(std::uint32_t);
            auto bar = read<std::uint32_t>(offset);

            write<std::uint32_t>(offset, 0xFFFFFFFF);
            auto lenlow = read<std::uint32_t>(offset);
            write<std::uint32_t>(offset, bar);

            if (bar & 0x01)
            {
                std::uintptr_t addr = bar & ~0b11;
                std::size_t length = (~(lenlow & ~0b11) + 1) & 0xFFFF;

                ret.virt = addr;
                ret.phys = addr;
                ret.size = length;
                ret.type = bar::type::io;
                ret.prefetch = false;
            }
            else
            {
                std::size_t length = 0;
                std::uintptr_t addr = 0;

                switch (auto type = (bar >> 1) & 0x03)
                {
                    case 0x00:
                        length = ~(lenlow & ~0b1111) + 1;
                        addr = bar & ~0b1111;
                        break;
                    case 0x02:
                    {
                        if (i == nbars - 1)
                            continue;

                        auto offseth = offset + sizeof(std::uint32_t);
                        auto barh = read<std::uint32_t>(offseth);

                        write<std::uint32_t>(offseth, 0xFFFFFFFF);
                        auto lenhigh = read<std::uint32_t>(offseth);
                        write<std::uint32_t>(offseth, barh);

                        length = ~((static_cast<std::uint64_t>(lenhigh) << 32) | (lenlow & ~0b1111)) + 1;
                        addr = (static_cast<std::uint64_t>(barh) << 32) | (bar & ~0b1111);

                        bit64 = true;
                        break;
                    }
                    default:
                        lib::error("pci: unknown memory mapped bar type 0x{:X}", type);
                        break;
                }

                ret.phys = addr;
                ret.size = length;
                ret.type = bar::type::mem;
                ret.prefetch = bar & (1 << 3);
                ret.bits64 = bit64;

                if (ret.phys == 0 && ret.size == 0)
                    ret.type = bar::type::invalid;
            }

            if (ret.type != bar::type::invalid)
                lib::debug("pci: - bar: 0x{:X}, size: 0x{:X}, type: {}", ret.phys, ret.size, magic_enum::enum_name(ret.type));

            bars[i] = ret;
            if (bit64 == true)
                bars[++i] = { 0, 0, 0, false, true, bar::type::invalid };
        }
    }

    void addio(std::shared_ptr<configio> io, std::uint16_t seg, std::uint16_t bus)
    {
        lib::bug_on(!static_cast<bool>(io));
        const std::uint32_t idx = (seg << 8 | bus);
        lib::bug_on(ios.contains(idx));
        ios[idx] = io;
    }

    std::shared_ptr<configio> getio(std::uint16_t seg, std::uint8_t bus)
    {
        const std::uint32_t idx = (seg << 8 | bus);
        lib::bug_on(!ios.contains(idx));
        return ios[idx];
    }

    void addrb(std::shared_ptr<bus> rb)
    {
        lib::bug_on(!static_cast<bool>(rb));
        rbs.push_back(rb);
    }

    const lib::map::flat_hash<std::uint32_t, std::shared_ptr<bridge>> &bridges() { return brdgs; }
    const lib::map::flat_hash<std::uint32_t, std::shared_ptr<device>> &devices() { return devs; }

    namespace arch
    {
        lib::initgraph::stage *ios_discovered_stage()
        {
            static lib::initgraph::stage stage
            {
                "pci.arch.ios-discovered",
                lib::initgraph::presched_init_engine
            };
            return &stage;
        }

        lib::initgraph::stage *rbs_discovered_stage()
        {
            static lib::initgraph::stage stage
            {
                "pci.arch.rbs-discovered",
                lib::initgraph::presched_init_engine
            };
            return &stage;
        }
    } // namespace arch

    namespace acpi
    {
        lib::initgraph::stage *ios_discovered_stage();
        lib::initgraph::stage *rbs_discovered_stage();
    } // namespace acpi

    lib::initgraph::stage *ios_discovered_stage()
    {
        static lib::initgraph::stage stage
        {
            "pci.ios-discovered",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *rbs_discovered_stage()
    {
        static lib::initgraph::stage stage
        {
            "pci.rbs-discovered",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task ios_task
    {
        "pci.discover-ios",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            acpi::ios_discovered_stage(),
            arch::ios_discovered_stage()
        },
        lib::initgraph::entail { ios_discovered_stage() },
        [] { }
    };

    lib::initgraph::task rbs_task
    {
        "pci.discover-rbs",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            acpi::rbs_discovered_stage(),
            arch::rbs_discovered_stage()
        },
        lib::initgraph::entail { rbs_discovered_stage() },
        [] { }
    };

    lib::initgraph::stage *enumerated_stage()
    {
        static lib::initgraph::stage stage
        {
            "pci.enumerated",
            lib::initgraph::presched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task pci_task
    {
        "pci.enumerate",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { ios_discovered_stage(), rbs_discovered_stage() },
        lib::initgraph::entail { enumerated_stage() },
        [] {
            lib::info("pci: enumerating devices");

            if (ios.empty())
            {
                lib::error("pci: no config spaces found");
                return;
            }
            if (rbs.empty())
            {
                lib::error("pci: no root buses found");
                return;
            }

            for (const auto &rb : rbs)
            {
                lib::debug("pci: root bus: {:04X}:{:02X}", rb->seg, rb->id);
                enum_bus(rb);
            }
        }
    };

} // namespace pci