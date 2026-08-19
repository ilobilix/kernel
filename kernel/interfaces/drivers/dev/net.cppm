// Copyright (C) 2024-2026  ilobilo

export module drivers.dev.net;

export import drivers.dev;

import lib;
import std;

export namespace dev::net
{
    using mac_t = std::array<std::uint8_t, 6>;

    enum iff : std::uint32_t
    {
        iff_up = 1 << 0,
        iff_broadcast = 1 << 1,
        iff_debug = 1 << 2,
        iff_loopback = 1 << 3,
        iff_pointopoint = 1 << 4,
        iff_running = 1 << 6,
        iff_noarp = 1 << 7,
        iff_promisc = 1 << 8,
        iff_allmulti = 1 << 9,
        iff_master = 1 << 10,
        iff_slave = 1 << 11,
        iff_multicast = 1 << 12
    };

    enum class arphrd : std::uint16_t
    {
        ether = 1,
        loopback = 772
    };

    struct stats_t
    {
        std::atomic_uint64_t rx_packets, rx_bytes, rx_errors, rx_dropped;
        std::atomic_uint64_t tx_packets, tx_bytes, tx_errors, tx_dropped;
        std::atomic_uint64_t multicast, collisions;
    };

    class nic_t
    {
        friend lib::expect<void> register_nic(
            const std::shared_ptr<nic_t> &nic, std::weak_ptr<dev::kobject_t> parent
        );

        static std::uint32_t alloc_idx();

        protected:
        mac_t _mac;
        std::atomic_uint32_t _mtu;
        arphrd _type;
        std::atomic_uint32_t _flags;
        std::atomic_bool _carrier;
        stats_t _stats;

        std::string _name;
        std::uint32_t _index;

        virtual lib::expect<void> do_transmit(std::span<const std::byte> frame) = 0;

        public:
        std::shared_ptr<dev::device_t> dev;
        std::shared_ptr<void> lwip;

        nic_t(arphrd type = arphrd::ether)
            : _mac { }, _mtu { 1500 }, _type { type }, _flags { iff_broadcast | iff_multicast },
              _carrier { false }, _stats { }, _name { }, _index { alloc_idx() },
              dev { }, lwip { } { }

        const std::string &name() const { return _name; }
        std::uint32_t index() const { return _index; }

        const mac_t &mac() const { return _mac; };
        std::uint32_t mtu() const { return _mtu.load(std::memory_order_relaxed); }

        arphrd type() const { return _type; }
        std::uint32_t flags() const { return _flags.load(std::memory_order_relaxed); }
        bool carrier() const { return _carrier.load(std::memory_order_relaxed); }
        const stats_t &stats() const { return _stats; }

        void receive(std::span<const std::byte> frame);
        lib::expect<void> transmit(std::span<const std::byte> frame);

        void set_carrier(bool up);

        lib::expect<void> up();
        lib::expect<void> down();

        virtual lib::expect<void> start() = 0;
        virtual void stop() = 0;

        virtual lib::expect<void> set_mtu(std::uint32_t mtu);

        virtual lib::expect<void> set_mac(const mac_t &mac)
        {
            lib::unused(mac);
            return std::unexpected { lib::err::not_supported };
        }

        virtual ~nic_t() = default;
    };

    mac_t generate_mac();

    lib::expect<void> register_nic(
        const std::shared_ptr<nic_t> &nic, std::weak_ptr<dev::kobject_t> parent
    );
    bool unregister_nic(const std::shared_ptr<nic_t> &nic);

    std::shared_ptr<nic_t> by_ifindex(std::uint32_t idx);
    std::shared_ptr<nic_t> by_name(std::string_view name);
    std::vector<std::shared_ptr<nic_t>> all();

    class_t &get_class();
    ktype_t &get_ktype();

    lib::initgraph::stage *registered_stage();
} // export namespace dev::net
