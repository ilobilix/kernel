// Copyright (C) 2024-2026  ilobilo

export module system.pci:dev;

import system.dev;
import lib;
import std;
import fmt;

import :core;

namespace pci
{
    dev::bus_t *get_bus();

    std::string get_slot_name(const std::shared_ptr<pci::device> &dev);
    std::string get_modalias(const std::shared_ptr<pci::device> &dev);
} // namespace pci

export namespace pci
{
    struct ktype_t : dev::ktype_t
    {
        std::span<dev::attribute_t *const> attributes() override;
        std::span<dev::bin_attribute_t *const> bin_attributes() override;

        static ktype_t *instance()
        {
            static ktype_t ktype { };
            return &ktype;
        }
    };

    struct id_t
    {
        std::uint16_t vendor, device;
        std::uint32_t class_mask, class_val;

        id_t(
            std::uint16_t vendor, std::uint16_t device,
            std::uint32_t class_mask, std::uint32_t class_val
        ) : vendor { vendor }, device { device },
            class_mask { class_mask }, class_val { class_val } { }

        static std::uint32_t make_class(
            std::uint8_t progif, std::uint8_t subclass, std::uint8_t class_
        )
        {
            return (static_cast<std::uint32_t>(class_) << 16) |
                   (static_cast<std::uint32_t>(subclass) << 8) | progif;
        }

        static std::uint32_t make_class(const std::shared_ptr<pci::device> &dev)
        {
            return make_class(dev->progif, dev->subclass, dev->class_);
        }

        bool match(std::uint16_t ven, std::uint16_t dev, std::uint32_t cls) const
        {
            return vendor == ven && device == dev &&
                (class_mask == 0xFFFFFFFF || (cls & class_mask) == class_val);
        }

        bool match(const std::shared_ptr<pci::device> &dev) const
        {
            return match(dev->venid, dev->devid, make_class(dev));
        }
    };

    struct driver_t : dev::driver_t
    {
        std::span<const id_t> ids;

        driver_t(std::string_view name, std::span<const id_t> ids)
            : dev::driver_t { name, get_bus() }, ids { ids } { }
    };

    struct device_t : dev::device_t
    {
        std::shared_ptr<pci::device> dev;

        device_t(const std::shared_ptr<pci::device> &device)
            : dev::device_t { get_slot_name(device), ktype_t::instance() }, dev { device }
        {
            modalias = get_modalias(device);
        }
    };

    struct bus_t : dev::bus_t
    {
        bus_t() : dev::bus_t { "pci" } { }

        bool match(dev::device_t &dev, dev::driver_t &drv) override
        {
            const auto pcidev = static_cast<device_t *>(&dev)->dev;
            const auto pcidrv = static_cast<driver_t *>(&drv);

            for (const auto &id : pcidrv->ids)
            {
                if (id.match(pcidev))
                    return true;
            }
            return false;
        }

        void fill_uevent(dev::device_t &dev, dev::uevent_t &uev) override;
    };
} // export namespace pci
