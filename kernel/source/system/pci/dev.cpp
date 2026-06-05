// Copyright (C) 2024-2026  ilobilo

module system.pci;

namespace pci
{
    namespace
    {
        struct attribute_t : dev::attribute_t
        {
            using fn_t = lib::expect<std::string> (*)(
                dev::device_t &, std::shared_ptr<pci::device>
            );
            fn_t fn;

            attribute_t(fn_t fn, std::string_view name, mode_t mode)
                : dev::attribute_t { name, mode }, fn { fn } { }

            lib::expect<std::string> show(dev::kobject_t &kobj) override
            {
                const auto device = kobj.as_device();
                if (!device || device->bus != get_bus())
                    return std::unexpected { lib::err::io_error };

                auto dev = static_cast<device_t *>(device)->dev;
                if (!dev)
                    return std::unexpected { lib::err::io_error };

                if (!fn)
                    return dev::attribute_t::show(kobj);
                return fn(*device, std::move(dev));
            }
        };

        lib::expect<std::shared_ptr<device>> pci_device(dev::kobject_t &kobj)
        {
            const auto dev = kobj.as_device();
            if (!dev || dev->bus != get_bus())
                return std::unexpected { lib::err::io_error };

            auto pcidev = static_cast<device_t *>(dev)->dev;
            if (!pcidev)
                return std::unexpected { lib::err::io_error };
            return pcidev;
        }

        std::size_t config_space_size(const std::shared_ptr<device> &dev)
        {
            const auto bus = dev->parent.lock();
            const auto io = bus ? bus->io.lock() : nullptr;
            if (!dev->is_pcie || !io || io->size() < 4096)
                return 256;
            if (dev->read<std::uint32_t>(0x100) == 0xFFFFFFFF)
                return 256;
            return 4096;
        }

        struct config_attribute_t : dev::bin_attribute_t
        {
            using dev::bin_attribute_t::bin_attribute_t;

            std::size_t size(dev::kobject_t &kobj) override
            {
                auto dev = pci_device(kobj);
                return dev ? config_space_size(std::move(*dev)) : 0;
            }

            lib::expect<std::size_t> read(
                dev::kobject_t &kobj, std::span<std::byte> buffer, std::size_t offset
            ) override
            {
                const auto dres = pci_device(kobj);
                if (!dres)
                    return std::unexpected { dres.error() };
                const auto dev = std::move(*dres);

                const auto total = config_space_size(dev);
                if (offset >= total)
                    return 0uz;

                auto count = std::min(buffer.size(), total - offset);
                std::size_t pos = 0, off = offset, rem = count;

                const auto put = [&](std::uint32_t val, std::size_t width) {
                    for (std::size_t i = 0; i < width; i++)
                        buffer[pos + i] = static_cast<std::byte>((val >> (i * 8)) & 0xFF);
                };

                if ((off & 1) && rem >= 1)
                {
                    put(dev->read<std::uint8_t>(off), 1);
                    off += 1; pos += 1; rem -= 1;
                }
                if ((off & 3) && rem > 2)
                {
                    put(dev->read<std::uint16_t>(off), 2);
                    off += 2; pos += 2; rem -= 2;
                }
                for (; rem > 3; off += 4, pos += 4, rem -= 4)
                    put(dev->read<std::uint32_t>(off), 4);
                if (rem >= 2)
                {
                    put(dev->read<std::uint16_t>(off), 2);
                    off += 2; pos += 2; rem -= 2;
                }
                if (rem >= 1)
                    put(dev->read<std::uint8_t>(off), 1);

                return count;
            }

            lib::expect<std::size_t> write(
                dev::kobject_t &kobj, std::span<const std::byte> buffer, std::size_t offset
            ) override
            {
                const auto dres = pci_device(kobj);
                if (!dres)
                    return std::unexpected { dres.error() };
                const auto dev = std::move(*dres);

                const auto total = config_space_size(dev);
                if (offset >= total)
                    return 0uz;

                auto count = std::min(buffer.size(), total - offset);
                std::size_t pos = 0, off = offset, rem = count;

                const auto get = [&](std::size_t width) {
                    std::uint32_t val = 0;
                    for (std::size_t i = 0; i < width; i++)
                        val |= static_cast<std::uint32_t>(buffer[pos + i]) << (i * 8);
                    return val;
                };

                if ((off & 1) && rem >= 1)
                {
                    dev->write<std::uint8_t>(off, get(1));
                    off += 1, pos += 1, rem -= 1;
                }
                if ((off & 3) && rem > 2)
                {
                    dev->write<std::uint16_t>(off, get(2));
                    off += 2, pos += 2, rem -= 2;
                }
                for (; rem > 3; off += 4, pos += 4, rem -= 4)
                    dev->write<std::uint32_t>(off, get(4));
                if (rem >= 2)
                {
                    dev->write<std::uint16_t>(off, get(2));
                    off += 2, pos += 2, rem -= 2;
                }
                if (rem >= 1)
                    dev->write<std::uint8_t>(off, get(1));

                return count;
            }
        };

        attribute_t ven {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->venid);
            }, "vendor", 0444
        };

        attribute_t dev {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->devid);
            }, "device", 0444
        };

        attribute_t cls {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:06x}\n", id_t::make_class(dev));
            }, "class", 0444
        };

        attribute_t subsysven {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->subsysvenid);
            }, "subsystem_vendor", 0444
        };

        attribute_t subsysdev {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->subsysdevid);
            }, "subsystem_device", 0444
        };

        config_attribute_t config { "config", 0644 };
    } // namespace

    dev::bus_t *get_bus()
    {
        static bus_t bus { };
        return &bus;
    }

    std::string get_slot_name(const std::shared_ptr<pci::device> &dev)
    {
        return fmt::format(
            "0000:{:02x}:{:02x}.{:02x}",
            dev->parent.lock()->id, dev->dev, dev->func
        );
    }

    std::string get_modalias(const std::shared_ptr<pci::device> &dev)
    {
        return fmt::format(
            "pci:v{:08x}d{:08x}sv{:08x}sd{:08x}bc{:02x}sc{:02x}i{:02x}",
            dev->venid, dev->devid, dev->subsysvenid, dev->subsysdevid,
            dev->class_, dev->subclass, dev->progif
        );
    }

    std::span<dev::attribute_t *const> ktype_t::attributes()
    {
        // TODO: revision, irq, local_cpus, etc
        static dev::attribute_t *list[] {
            &ven, &dev, &cls, &subsysven, &subsysdev
        };
        return list;
    }

    std::span<dev::bin_attribute_t *const> ktype_t::bin_attributes()
    {
        static dev::bin_attribute_t *list[] { &config };
        return list;
    }

    void bus_t::fill_uevent(dev::device_t &dev, dev::uevent_t &uev)
    {
        const auto pcidev = static_cast<device_t *>(&dev)->dev;
        uev.add("PCI_CLASS", fmt::format("{:02x}{:02x}{:02x}",
            pcidev->class_, pcidev->subclass, pcidev->progif
        ));
        uev.add("PCI_ID", fmt::format("{:04x}:{:04x}", pcidev->venid, pcidev->devid));
        uev.add("PCI_SUBSYS_ID", fmt::format("{:04x}:{:04x}",
            pcidev->subsysvenid, pcidev->subsysdevid)
        );
        uev.add("PCI_SLOT_NAME", dev.name);
        uev.add("MODALIAS", dev.modalias);
    }
} // namespace pci
