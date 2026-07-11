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
        static constexpr std::uint32_t any = 0xFFFFFFFF;

        std::uint32_t vendor, device;
        std::uint32_t subvendor, subdevice;
        std::uint32_t class_val, class_mask;

        static constexpr id_t from_id(std::uint32_t vendor, std::uint32_t device)
        {
            return { vendor, device, any, any, 0, 0 };
        }

        static constexpr id_t from_subid(
            std::uint32_t vendor, std::uint32_t device,
            std::uint32_t subvendor, std::uint32_t subdevice
        )
        {
            return { vendor, device, subvendor, subdevice, 0, 0 };
        }

        static constexpr id_t from_class(std::uint32_t class_val, std::uint32_t class_mask)
        {
            return { any, any, any, any, class_val, class_mask };
        }

        static constexpr id_t from_class(
            std::uint8_t class_, std::uint8_t subclass,
            std::uint8_t progif, std::uint32_t class_mask = any
        )
        {
            return { any, any, any, any, make_class(class_, subclass, progif), class_mask };
        }

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

        constexpr bool match(
            std::uint16_t ven, std::uint16_t dev,
            std::uint16_t subven, std::uint16_t subdev, std::uint32_t cls
        ) const
        {
            return (vendor == any || vendor == ven) &&
                   (device == any || device == dev) &&
                   (subvendor == any || subvendor == subven) &&
                   (subdevice == any || subdevice == subdev) &&
                   (class_mask == 0 || (cls & class_mask) == class_val);
        }

        bool match(const std::shared_ptr<pci::device> &dev) const
        {
            return match(dev->venid, dev->devid, dev->subvenid, dev->subdevid, make_class(dev));
        }

        constexpr auto get_formatted_parts() const
        {
            const auto get_id = [](std::string_view tag, std::uint32_t id) {
                if (id == any)
                    return fmt::format("{}*"_cf, tag);
                return fmt::format("{}{:08x}"_cf, tag, id);
            };

            const auto get_byte = [&](std::string_view tag, auto shift, bool last = false) {
                if (((class_mask >> shift) & 0xFF) == 0xFF)
                    return fmt::format("{}{:02x}"_cf, tag, (class_val >> shift) & 0xFF);
                return fmt::format("{}{}"_cf, tag, last ? "" : "*");
            };

            return std::tuple {
                get_id("v", vendor), get_id("d", device),
                get_id("sv", subvendor), get_id("sd", subdevice),
                get_byte("bc", 16), get_byte("sc",  8), get_byte("i", 0, true)
            };
        }

        std::string get_modalias() const
        {
            return fmt::format("pci:{}*", fmt::join(get_formatted_parts(), ""));
        }
    };

    template<id_t Id>
    consteval auto get_modalias()
    {
        constexpr auto parts = Id.get_formatted_parts();
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return lib::consteval_format<
                "pci:{}{}{}{}{}{}{}*"_cf,
                lib::comptime_string<std::get<Is>(parts).size() + 1> {
                    std::get<Is>(parts).data()
                } ...
            >();
        } (std::make_index_sequence<std::tuple_size_v<decltype(parts)>> { });
    }

    static_assert(
        "pci:v0000deadd0000beefsv*sd*bc*sc*i*"sv ==
        get_modalias<id_t::from_id(0xDEAD, 0xBEEF)>()
    );

    static_assert(
        "pci:v*d*sv*sd*bc01sc08i02*"sv ==
        get_modalias<id_t::from_class(0x01, 0x08, 0x02, id_t::any)>()
    );

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
            return std::make_shared<device_t>(lib::private_t<device_t> { }, device, parent);
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

export namespace mod
{
    template<pci::id_t Id>
    struct modalias_generator<Id>
    {
        static consteval auto get()
        {
            return pci::get_modalias<Id>();
        }
    };
} // export namespace mod
