// Copyright (C) 2024-2026  ilobilo

export module system.virtio;

export import :spec;
export import :queue;

import system.dev;
import fmt;
import lib;
import std;

namespace virtio
{
    dev::bus_t *get_bus();
    dev::ktype_t &get_ktype();
    dev::ktype_t &get_driver_ktype();
} // namespace virtio

export namespace virtio
{
    struct id_t
    {
        static constexpr std::uint32_t any = 0xFFFFFFFF;

        std::uint32_t device;
        std::uint32_t vendor;

        static constexpr id_t from_type(device_type type, std::uint32_t vendor = any)
        {
            return { std::to_underlying(type), vendor };
        }

        constexpr bool match(std::uint32_t dev, std::uint32_t ven) const
        {
            return (device == any || device == dev) &&
                   (vendor == any || vendor == ven);
        }

        constexpr auto get_formatted_parts() const
        {
            const auto part = [](std::string_view tag, std::uint32_t val) {
                if (val == any)
                    return fmt::format("{}*"_cf, tag);
                return fmt::format("{}{:08x}"_cf, tag, val);
            };
            return std::tuple { part("d", device), part("v", vendor) };
        }

        std::string get_modalias() const
        {
            return fmt::format("virtio:{}", fmt::join(get_formatted_parts(), ""));
        }
    };

    template<id_t Id>
    consteval auto get_modalias()
    {
        constexpr auto parts = Id.get_formatted_parts();
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return lib::consteval_format<
                "virtio:{}{}"_cf,
                lib::comptime_string<std::get<Is>(parts).size() + 1> {
                    std::get<Is>(parts).data()
                } ...
            >();
        } (std::make_index_sequence<std::tuple_size_v<decltype(parts)>> { });
    }

    static_assert(
        "virtio:d00000001v00001af4"sv ==
        get_modalias<id_t::from_type(device_type::network, 0x1AF4)>()
    );

    static_assert(
        "virtio:d00000010v*"sv ==
        get_modalias<id_t::from_type(device_type::gpu)>()
    );

    // TODO
    struct queue_t { };
    using used_fn = void (*)();

    struct queue_addr_t
    {
        std::uintptr_t desc, avail, used;
        std::uint16_t size;
    };

    class transport_t
    {
        public:
        virtual std::string_view type() const = 0;

        virtual std::uint64_t device_features() = 0;
        virtual void driver_features(std::uint64_t feat) = 0;

        virtual std::uint64_t mandatory_features() const = 0;

        virtual std::uint8_t status() = 0;
        virtual void add_status(std::uint8_t bits) = 0;
        virtual void reset() = 0;

        virtual std::uint16_t num_queues() = 0;
        virtual std::uint16_t queue_max_size(std::uint16_t qid) = 0;
        virtual lib::expect<void> enable_queue(std::uint16_t qid, const queue_addr_t &addr) = 0;
        virtual void disable_queue(std::uint16_t qid) = 0;
        virtual void notify(std::uint16_t qid) = 0;

        virtual void read_config(std::size_t off, std::span<std::byte> buffer) = 0;
        virtual void write_config(std::size_t off, std::span<const std::byte> buffer) = 0;
        virtual std::uint8_t config_generation() = 0;

        // TODO
        // virtual lib::expect<irq_alloc_t> setup_irqs(std::uint16_t nqueues, std::size_t cpu) = 0;
        virtual void release_irqs() = 0;
        virtual std::uint8_t isr_status() { return 0; }

        virtual ~transport_t() = default;
    };

    class device_t final : public dev::device_t
    {
        friend struct bus_t;

        private:
        std::unique_ptr<transport_t> _transport;
        std::uint64_t _features;
        std::vector<std::unique_ptr<queue_t>> _queues;
        std::function<void ()> _config_changed;
        std::atomic_bool _ready = false;

        static inline std::atomic_size_t next_index = 0;
        static std::string alloc_name()
        {
            return fmt::format("virtio{}", next_index.fetch_add(1, std::memory_order_relaxed));
        }

        device_t(
            std::unique_ptr<transport_t> tp,
            id_t ident, std::weak_ptr<dev::kobject_t> parent
        ) : dev::device_t { alloc_name(), get_ktype(), parent }, _transport { std::move(tp) },
            _features { 0 }, _queues { }, _config_changed { }, ident { ident }
        {
            bus = get_bus();
            modalias = ident.get_modalias();
        }

        public:
        const id_t ident;

        template<typename ...Args>
        device_t(lib::private_t<device_t>, Args &&...args)
            : device_t { std::forward<Args>(args)... } { };

        device_t(const device_t &) = delete;
        device_t &operator=(const device_t &) = delete;

        static std::shared_ptr<device_t> create(
            std::unique_ptr<transport_t> tp,
            id_t ident, std::weak_ptr<dev::kobject_t> parent
        )
        {
            return std::make_shared<device_t>(
                lib::private_t<device_t> { }, std::move(tp), ident, parent
            );
        }

        std::uint64_t features() const { return _features; }
        bool has(feature feat) const { return _features & feature_bit(feat); }

        transport_t &transport() { return *_transport; }

        void on_config_change(std::function<void ()> fn)
        {
            _config_changed = std::move(fn);
        }

        lib::expect<queue_t *> setup_queue(
            std::uint16_t qid, used_fn on_used, std::uint16_t size = 0
        );
        lib::expect<std::vector<queue_t *>> setup_queues(std::span<const used_fn> fns);
        queue_t &queue(std::uint16_t qid);

        template<typename Type>
            requires std::is_trivially_copyable_v<Type>
        Type read_config(std::size_t off)
        {
            Type value { };
            _transport->read_config(off, std::as_writable_bytes(std::span { &value, 1 }));
            return value;
        }

        template<typename Type>
            requires std::is_trivially_copyable_v<Type>
        void write_config(std::size_t off, Type value)
        {
            _transport->write_config(off, std::as_bytes(std::span { &value, 1 }));
        }

        template<typename Type>
            requires std::is_trivially_copyable_v<Type>
        Type read_config_stable(std::size_t off)
        {
            while (true)
            {
                const auto before = _transport->config_generation();
                const auto value = read_config<Type>(off);
                if (before == _transport->config_generation())
                    return value;
            }
        }

        void set_ready();
        void destroy();
    };

    class driver_t : public dev::driver_t
    {
        private:
        const std::span<const id_t> _ids;

        public:
        driver_t(std::string_view name, std::span<const id_t> ids)
            : dev::driver_t { name, get_bus(), get_driver_ktype() }, _ids { ids } { }

        virtual std::uint64_t features() const { return 0; }

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

        bool matches(const device_t &dev) const
        {
            return std::ranges::any_of(_ids, [&](const id_t &id) {
                return id.match(dev.ident.device, dev.ident.vendor);
            });
        }
    };

    struct bus_t final : dev::bus_t
    {
        bus_t() : dev::bus_t { "virtio", dev::bus_ktype() } { }

        bool match(dev::device_t &dev, dev::driver_t &drv) const override
        {
            return static_cast<driver_t &>(drv).matches(static_cast<device_t &>(dev));
        }

        lib::expect<void> probe(dev::device_t &dev, dev::driver_t &drv) override;
        bool remove(dev::device_t &dev, dev::driver_t &drv) override;
        void fill_uevent(dev::device_t &dev, dev::uevent_t &uev) override;
    };

    lib::initgraph::stage *bus_registered_stage();
} // export namespace virtio

export namespace mod
{
    template<virtio::id_t Id>
    struct modalias_generator<Id>
    {
        static consteval auto get()
        {
            return virtio::get_modalias<Id>();
        }
    };
} // export namespace mod
