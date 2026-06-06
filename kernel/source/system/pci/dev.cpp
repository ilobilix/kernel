// Copyright (C) 2024-2026  ilobilo

module system.pci;

import system.bin.elf;

namespace pci
{
    namespace
    {
        struct attribute_t : dev::attribute_t
        {
            using rfn_t = lib::expect<std::string> (*)(
                dev::device_t &, std::shared_ptr<pci::device>
            );
            using wfn_t = lib::expect<void> (*)(
                dev::device_t &, std::shared_ptr<pci::device>, std::string_view
            );

            rfn_t rfn;
            wfn_t wfn;

            attribute_t(rfn_t rfn, wfn_t wfn, std::string_view name, mode_t mode)
                : dev::attribute_t { name, mode }, rfn { rfn }, wfn { wfn } { }

            lib::expect<std::string> show(dev::kobject_t &kobj) override
            {
                const auto device = kobj.as_device();
                if (!device || device->bus != get_bus())
                    return std::unexpected { lib::err::io_error };

                auto dev = static_cast<device_t *>(device)->dev;
                if (!dev)
                    return std::unexpected { lib::err::io_error };

                if (!rfn)
                    return dev::attribute_t::show(kobj);
                return rfn(*device, std::move(dev));
            }

            lib::expect<void> store(dev::kobject_t &kobj, std::string_view value) override
            {
                const auto device = kobj.as_device();
                if (!device || device->bus != get_bus())
                    return std::unexpected { lib::err::io_error };

                auto dev = static_cast<device_t *>(device)->dev;
                if (!dev)
                    return std::unexpected { lib::err::io_error };

                if (!wfn)
                    return dev::attribute_t::store(kobj, value);
                return wfn(*device, std::move(dev), value);
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
            if (dev->config_size != 0)
                return dev->config_size;

            const auto bus = dev->parent.lock();
            const auto io = bus ? bus->io.lock() : nullptr;

            std::size_t size = 256;
            if (dev->is_pcie && io && io->size() >= 4096 &&
                dev->read<std::uint32_t>(0x100) != 0xFFFFFFFF)
                size = 4096;

            return dev->config_size = size;
        }

        struct config_attribute_t : dev::bin_attribute_t
        {
            config_attribute_t() : bin_attribute_t { "config", 0644 } { }

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

        driver_t &driver_of(dev::kobject_t &kobj)
        {
            return static_cast<driver_t &>(static_cast<dev::driver_kobject_t &>(kobj).drv);
        }

        std::optional<id_t> parse_id(std::string_view data)
        {
            constexpr std::string_view ws { " \t\r\n" };

            std::vector<std::string_view> toks;
            std::size_t pos = 0;
            while ((pos = data.find_first_not_of(ws, pos)) != std::string_view::npos)
            {
                const auto end = data.find_first_of(ws, pos);
                toks.push_back(data.substr(pos, end - pos));
                pos = end;
            }

            if (toks.size() < 2)
                return std::nullopt;

            const auto parse_hex = [](std::string_view tok) -> std::optional<std::uint32_t> {
                char *end = nullptr;
                const auto value = lib::str2int<std::uint32_t>(tok.data(), &end, 16);
                if (!value || end != tok.data() + tok.size())
                    return std::nullopt;
                return value;
            };

            const auto vendor = parse_hex(toks[0]);
            const auto device = parse_hex(toks[1]);
            if (!vendor || !device)
                return std::nullopt;

            std::uint32_t class_val = 0;
            std::uint32_t class_mask = 0xFFFFFFFF;
            if (toks.size() >= 4)
            {
                const auto cv = parse_hex(toks[2]);
                const auto cm = parse_hex(toks[3]);
                if (!cv || !cm)
                    return std::nullopt;

                class_val = *cv;
                class_mask = *cm;
            }

            return id_t {
                static_cast<std::uint16_t>(*vendor),
                static_cast<std::uint16_t>(*device),
                class_mask, class_val
            };
        }

        struct new_id_attribute_t : dev::attribute_t
        {
            new_id_attribute_t() : dev::attribute_t { "new_id", 0200 } { }

            lib::expect<void> store(dev::kobject_t &kobj, std::string_view data) override
            {
                const auto id = parse_id(data);
                if (!id)
                    return std::unexpected { lib::err::invalid_argument };

                auto &drv = driver_of(kobj);
                drv.add_id(*id);
                dev::probe_driver(drv);
                return { };
            }
        };

        struct remove_id_attribute_t : dev::attribute_t
        {
            remove_id_attribute_t() : dev::attribute_t { "remove_id", 0200 } { }

            lib::expect<void> store(dev::kobject_t &kobj, std::string_view data) override
            {
                const auto id = parse_id(data);
                if (!id)
                    return std::unexpected { lib::err::invalid_argument };

                if (!driver_of(kobj).remove_id(id->vendor, id->device))
                    return std::unexpected { lib::err::no_such_device };
                return { };
            }
        };

        attribute_t ven {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->venid);
            }, nullptr, "vendor", 0444
        };

        attribute_t dev {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->devid);
            }, nullptr, "device", 0444
        };

        attribute_t cls {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:06x}\n", id_t::make_class(dev));
            }, nullptr, "class", 0444
        };

        attribute_t subsysven {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->subsysvenid);
            }, nullptr, "subsystem_vendor", 0444
        };

        attribute_t subsysdev {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:04x}\n", dev->subsysdevid);
            }, nullptr, "subsystem_device", 0444
        };

        attribute_t modalias {
            [](dev::device_t &dev, std::shared_ptr<device>) -> lib::expect<std::string> {
                return dev.modalias + "\n";
            }, nullptr, "modalias", 0444
        };

        attribute_t revision {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("0x{:02x}\n", dev->revision);
            }, nullptr, "revision", 0444
        };

        attribute_t enable {
            [](dev::device_t &, std::shared_ptr<device> dev) -> lib::expect<std::string> {
                return fmt::format("{}\n", dev->enable_count);
            },
            [](dev::device_t &, std::shared_ptr<device> dev, std::string_view data) -> lib::expect<void> {
                data = lib::trim(data);
                if (data == "1")
                {
                    if (dev->enable_count++ == 0)
                    {
                        // TODO
                    }
                }
                else if (data == "0")
                {
                    if (dev->enable_count == 0)
                        return std::unexpected { lib::err::io_error };

                    if (dev->enable_count-- == 1)
                    {
                        // TODO
                    }
                }
                else return std::unexpected { lib::err::invalid_argument };
                return { };
            },
            "enable", 0644
        };

        // TODO: resource, irq, local_cpus, rom

        config_attribute_t config { };

        new_id_attribute_t new_id_attribute { };
        remove_id_attribute_t remove_id_attribute { };

        struct ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t *const> attributes() override
            {
                static dev::attribute_t *list[] {
                    &ven, &dev, &cls, &subsysven, &subsysdev,
                    &modalias, &revision, &enable
                };
                return list;
            }

            std::span<dev::bin_attribute_t *const> bin_attributes() override
            {
                static dev::bin_attribute_t *list[] { &config };
                return list;
            }
        };

        struct driver_ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t *const> attributes() override
            {
                static dev::attribute_t *list[] {
                    dev::bind_attribute(), dev::unbind_attribute(),
                    &new_id_attribute, &remove_id_attribute
                };
                return list;
            }
        };
    } // namespace

    dev::bus_t *get_bus()
    {
        static bus_t bus { };
        return &bus;
    }

    dev::ktype_t *get_ktype()
    {
        static ktype_t ktype { };
        return &ktype;
    }

    dev::ktype_t *get_driver_ktype()
    {
        static driver_ktype_t ktype { };
        return &ktype;
    }

    std::string get_slot_name(const std::shared_ptr<pci::device> &dev)
    {
        return fmt::format(
            "0000:{:02x}:{:02x}.{:x}",
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

    std::string id_t::get_modalias() const
    {
        const auto id = [](const char *tag, std::uint16_t id) {
            if (id == 0xFFFF)
                return fmt::format("{}*", tag);
            return fmt::format("{}{:08x}", tag, id);
        };

        const auto byte = [&](const char *tag, auto shift) {
            if (((class_mask >> shift) & 0xFF) == 0xFF)
                return fmt::format("{}{:02x}", tag, (class_val >> shift) & 0xFF);
            return fmt::format("{}*", tag);
        };

        return fmt::format(
            "pci:{}{}sv*sd*{}{}{}*",
            id("v", vendor), id("d", device),
            byte("bc", 16), byte("sc", 8), byte("i", 0)
        );
    }

    bool driver_t::matches(const std::shared_ptr<pci::device> &dev)
    {
        const auto pred = [&](const id_t &id) { return id.match(dev); };
        return std::ranges::any_of(_ids, pred) ||
            std::ranges::any_of(*_dynamic_ids.read_lock(), pred);
    }

    void driver_t::add_id(const id_t &id)
    {
        _dynamic_ids.write_lock()->push_back(id);
    }

    bool driver_t::remove_id(std::uint16_t vendor, std::uint16_t device)
    {
        auto locked = _dynamic_ids.write_lock();
        const auto [first, last] = std::ranges::remove_if(*locked, [&](const id_t &id) {
            return id.vendor == vendor && id.device == device;
        });
        if (first == last)
            return false;
        locked->erase(first, last);
        return true;
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
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "pci.dev.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    namespace
    {
        lib::initgraph::task dev_task
        {
            "pci.dev.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require {
                bin::elf::mod::modules_loaded_stage(),
                dev::available_stage(),
                enumerated_stage()
            },
            lib::initgraph::entail { registered_stage() },
            [] {
                lib::bug_on(!dev::register_bus(*get_bus()));

                auto host = std::make_shared<dev::kobject_t>(
                    "pci0000:00",
                    dev::default_ktype(),
                    dev::devices_root()
                );
                lib::bug_on(!dev::register_kobject(host));

                for (const auto &[_, device] : devices())
                {
                    auto ddev = std::make_shared<device_t>(device);
                    ddev->parent = host;
                    lib::bug_on(!dev::register_device(ddev));
                }
            }
        };
    } // namespace
} // namespace pci
