// Copyright (C) 2024-2026  ilobilo

export module system.pci:dev;

import system.dev;
import lib;
import std;
import fmt;

import :core;

namespace pci
{
    // exported for fbdev
    export dev::bus_t *get_bus();
    dev::ktype_t &get_ktype();
    dev::ktype_t &get_driver_ktype();

    std::string get_slot_name(const std::shared_ptr<device> &dev);
    std::string get_modalias(const std::shared_ptr<device> &dev);
} // namespace pci

export namespace pci
{
    struct id_t
    {
        std::uint16_t vendor, device;
        std::uint32_t class_mask, class_val;

        constexpr id_t(
            std::uint16_t vendor, std::uint16_t device,
            std::uint32_t class_mask, std::uint32_t class_val
        ) : vendor { vendor }, device { device },
            class_mask { class_mask }, class_val { class_val } { }

        static constexpr std::uint32_t make_class(
            std::uint8_t class_, std::uint8_t subclass, std::uint8_t progif
        )
        {
            return (static_cast<std::uint32_t>(class_) << 16) |
                   (static_cast<std::uint32_t>(subclass) << 8) | progif;
        }

        static std::uint32_t make_class(const std::shared_ptr<pci::device> &dev)
        {
            return make_class(dev->class_, dev->subclass, dev->progif);
        }

        bool constexpr match(std::uint16_t ven, std::uint16_t dev, std::uint32_t cls) const
        {
            return (vendor == 0xFFFF || vendor == ven) &&
                   (device == 0xFFFF || device == dev) &&
                   (class_mask == 0 || (cls & class_mask) == class_val);
        }

        bool match(const std::shared_ptr<pci::device> &dev) const
        {
            return match(dev->venid, dev->devid, make_class(dev));
        }

        std::string get_modalias() const;
    };

    class device_t final : public dev::device_t
    {
        private:
        device_t(const std::shared_ptr<device> &device, std::weak_ptr<dev::kobject_t> parent)
            : dev::device_t { get_slot_name(device), get_ktype(), parent }, dev { device },
              config_size { 0 }, enable_count { 0 }
        {
            bus = get_bus();
            modalias = get_modalias(device);
        }

        public:
        const std::shared_ptr<device> dev;

        std::size_t config_size;
        std::size_t enable_count;

        template<typename ...Args>
        device_t(lib::private_t<device_t>, Args &&...args)
            : device_t { std::forward<Args>(args)... } { };

        device_t(const device_t &) = delete;
        device_t &operator=(const device_t &) = delete;

        static std::shared_ptr<device_t> create(
            const std::shared_ptr<device> &device, std::weak_ptr<dev::kobject_t> parent
        )
        {
            return std::make_shared<device_t>(
                lib::private_t<device_t> { }, device, parent
            );
        }
    };

    class driver_t : public dev::driver_t
    {
        private:
        lib::locker<std::vector<id_t>, lib::rwspinlock> _dynamic_ids;
        const std::span<const id_t> _ids;

        public:
        driver_t(std::string_view name, std::span<const id_t> ids)
            : dev::driver_t { name, get_bus(), get_driver_ktype() }, _ids { ids } { }

        virtual lib::expect<void> probe(device_t &dev) = 0;
        virtual bool remove(device_t &dev) = 0;

        lib::expect<void> probe(dev::device_t &dev) override
        {
            return probe(static_cast<device_t &>(dev));
        }

        bool remove(dev::device_t &dev) override
        {
            return remove(static_cast<device_t &>(dev));
        }

        bool matches(const std::shared_ptr<device> &dev);

        void add_id(const id_t &id);
        bool remove_id(std::uint16_t vendor, std::uint16_t device);
    };

    struct bus_t final : public dev::bus_t
    {
        bus_t() : dev::bus_t { "pci", dev::bus_ktype() } { }

        bool match(dev::device_t &dev, dev::driver_t &drv) const override
        {
            return static_cast<driver_t &>(drv).matches(static_cast<device_t &>(dev).dev);
        }

        void fill_uevent(dev::device_t &dev, dev::uevent_t &uev) override;
    };

    lib::initgraph::stage *registered_stage();
} // export namespace pci
