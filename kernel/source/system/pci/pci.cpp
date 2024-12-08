// Copyright (C) 2024  ilobilo

module system.pci;

import system.acpi;
import lib;
import std;

namespace pci
{
    namespace
    {
        lib::map::flat_hash<std::uint32_t, std::shared_ptr<configio>> ios;
        std::vector<std::shared_ptr<bus>> rbs;

        std::vector<std::shared_ptr<bridge>> bridges;
        std::vector<std::shared_ptr<device>> devs;

        void get_capabilites(auto &device, auto &&extra)
        {
            if (auto status = device->template read<16>(reg::status); status & (1 << 4))
            {
                auto offset = device->template read<16>(reg::capabilities) & 0xFC;
                if (offset != 0)
                    log::debug("  - Capabilities:");

                while (offset)
                {
                    const auto entry = device->template read<16>(offset);
                    switch (auto id = entry & 0xFF; id)
                    {
                        case 0x10:
                        {
                            log::debug("  -  PCIe");
                            device->is_pcie = true;
                            auto tp = (device->template read<16>(offset + 2) >> 4) & 0x0F;
                            device->is_secondary = (tp == 4 || tp == 6 || tp == 8);
                            break;
                        }
                        default:
                            if (!extra(id, offset))
                                log::debug("  -  Unknown: 0x{:X}", id);
                            break;
                    }

                    offset = (entry >> 8) & 0xFC;
                }
            }
        }

        void enum_bus(const auto &bus);
        void enum_func(const auto &bus, std::uint8_t dev, std::uint8_t func)
        {
            const auto venid = bus->template read<16>(dev, func, reg::venid);
            const auto devid = bus->template read<16>(dev, func, reg::devid);
            if (venid == 0xFFFF || devid == 0xFFFF)
                return;

            const auto header = bus->template read<8>(dev, func, reg::header);
            if (header == 0x00) // device
            {
                log::debug("- General device: {:04X}:{:04X}", venid, devid);

                const auto progif = bus->template read<8>(dev, func, reg::progif);
                const auto subclass = bus->template read<8>(dev, func, reg::subclass);
                const auto class_ = bus->template read<8>(dev, func, reg::class_);

                auto device = std::make_shared<pci::device>(bus, dev, func);
                device->venid = venid;
                device->devid = devid;
                device->progif = progif;
                device->subclass = subclass;
                device->class_ = class_;

                get_capabilites(device, [](auto id, auto offset)
                {
                    if (acpi::fadt && (acpi::fadt->iapc_boot_arch & (1 << 3)))
                        return false;

                    switch (id)
                    {
                        case 0x5:
                            log::debug("  -  MSI");
                            // TODO
                            break;
                        case 0x11:
                            log::debug("  -  MSI-X");
                            // TODO
                            break;
                        default:
                            return false;
                    }
                    return true;

                    lib::unused(offset);
                });

                const auto pin = device->template read<8>(reg::intpin);
                if (pin != 0 && bus->router)
                    device->irq.route = bus->router->resolve(dev, pin);

                devs.push_back(device);
                bus->devices.push_back(device);
            }
            else if (header == 0x01) // PCI-to-PCI bridge
            {
                log::debug("- PCI-to-PCI: {:04X}:{:04X}", venid, devid);
                auto bridge = std::make_shared<pci::bridge>(bus, dev, func);

                get_capabilites(bridge, [](auto ...) { return false; });

                const auto secondary_id = bridge->template read<8>(reg::secondary_bus);
                if (secondary_id)
                {
                    bridge->secondary_bus = secondary_id;
                    bridge->subordinate_bus = bridge->template read<8>(reg::subordinate_bus);

                    auto secondary_bus = std::make_shared<pci::bus>(bus->seg, secondary_id, bus->io, bridge, nullptr);
                    if (bus->router)
                        secondary_bus->router = bus->router->downstream(secondary_bus);

                    bridge->parent = secondary_bus;
                    enum_bus(secondary_bus);
                }

                bridges.push_back(bridge);
            }
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
            const bool devs32 = !(bus->bridge && bus->bridge->is_pcie && bus->bridge->is_secondary);
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

    void addio(std::shared_ptr<configio> io, std::uint16_t seg, std::uint16_t bus)
    {
        lib::ensure(static_cast<bool>(io));
        const std::uint32_t idx = (seg << 8 | bus);
        lib::ensure(!ios.contains(idx));
        ios[idx] = io;
    }

    std::shared_ptr<configio> getio(std::uint16_t seg, std::uint8_t bus)
    {
        const std::uint32_t idx = (seg << 8 | bus);
        lib::ensure(ios.contains(idx));
        return ios[idx];
    }

    void addrb(std::shared_ptr<bus> rb)
    {
        lib::ensure(static_cast<bool>(rb));
        rbs.push_back(rb);
    }

    const std::vector<std::shared_ptr<device>> &devices() { return devs; }

    void init()
    {
        log::info("Enumerating PCI devices");

        if (ios.empty())
        {
            log::error("PCI: No config spaces found");
            return;
        }
        if (rbs.empty())
        {
            log::error("PCI: No root buses found");
            return;
        }

        for (const auto &rb : rbs)
            enum_bus(rb);
    }

    namespace arch
    {
        void register_ios();
        void register_rbs();
    } // namespace arch

    namespace acpi
    {
        bool register_ios();
        bool register_rbs();
    } // namespace acpi

    void register_ios()
    {
        if (!acpi::register_ios())
            arch::register_ios();
    }

    void register_rbs()
    {
        if (!acpi::register_rbs())
            arch::register_rbs();
    }
} // namespace pci