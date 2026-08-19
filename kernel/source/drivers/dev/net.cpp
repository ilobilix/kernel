// Copyright (C) 2024-2026  ilobilo

module;

#include <lwip/pbuf.h>
#include <lwip/netif.h>
#include <lwip/netifapi.h>

module drivers.dev.net;

import drivers.fs.procfs;
import system.sched.mutex;
import system.random;
import system.rcu;
import lwip;
import fmt;

namespace dev::net
{
    namespace
    {
        std::atomic_uint32_t next_idx = 1;
        std::atomic_uint32_t next_ethn = 0;

        rcu::pointer<rcu::box<
            lib::map::flat_hash<
                std::uint32_t,
                std::shared_ptr<nic_t>
            >
        >> nics;
        sched::mutex_t write_lock;

        struct data_t
        {
            std::weak_ptr<nic_t> nic;
        };

        class net_class_t : public class_t
        {
            public:
            net_class_t() : class_t { "net", empty_ktype(), false } { }
        };

        struct nic_ktype_t : ktype_t
        {
            static std::shared_ptr<nic_t> nic_from(device_t &device)
            {
                if (!device.private_data)
                    return nullptr;
                return std::static_pointer_cast<data_t>(device.private_data)->nic.lock();
            }

            std::span<attribute_t *const> attributes() const override
            {
                struct nic_attribute_t : make_attribute_t
                {
                    using rfn_t = lib::expect<std::string> (*)(
                        std::shared_ptr<nic_t>
                    );
                    using wfn_t = lib::expect<void> (*)(
                        std::shared_ptr<nic_t>, std::string_view
                    );

                    nic_attribute_t(rfn_t rfn, wfn_t wfn, std::string_view name, mode_t mode)
                        : make_attribute_t {
                            [rfn](device_t &dev) -> lib::expect<std::string> {
                                auto nic = nic_from(dev);
                                if (!nic)
                                    return std::unexpected { lib::err::io_error };
                                return rfn(std::move(nic));
                            },
                            wfn == nullptr ? make_attribute_t::wfn_t { } :
                            [wfn](device_t &dev, std::string_view value) -> lib::expect<void> {
                                auto nic = nic_from(dev);
                                if (!nic)
                                    return std::unexpected { lib::err::io_error };
                                return wfn(std::move(nic), value);
                            }, name, mode
                        } { }
                };

                static nic_attribute_t address {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        return fmt::format("{:02x}\n", fmt::join(nic->mac(), ":"));
                    }, nullptr, "address", 0444
                };
                static nic_attribute_t broadcast {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        lib::unused(nic);
                        return "ff:ff:ff:ff:ff:ff\n";
                    }, nullptr, "broadcast", 0444
                };
                static nic_attribute_t ifindex {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        return fmt::format("{}\n", nic->index());
                    }, nullptr, "ifindex", 0444
                };
                static nic_attribute_t iflink {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        return fmt::format("{}\n", nic->index()); // TODO
                    }, nullptr, "iflink", 0444
                };
                static nic_attribute_t mtu {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        return fmt::format("{}\n", nic->mtu());
                    },
                    [](std::shared_ptr<nic_t> nic, std::string_view value) -> lib::expect<void> {
                        std::string data { lib::trim(value) };
                        char *end = nullptr;
                        const auto mtu = lib::str2int<std::uint32_t>(data.data(), &end, 10);
                        if (!mtu || end != data.data() + data.size())
                            return std::unexpected { lib::err::invalid_argument };
                        return nic->set_mtu(*mtu);
                    }, "mtu", 0644
                };
                static nic_attribute_t type {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        return fmt::format("{}\n", std::to_underlying(nic->type()));
                    }, nullptr, "type", 0444
                };
                static nic_attribute_t flags {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        return fmt::format("0x{:x}\n", nic->flags());
                    }, nullptr, "flags", 0444
                };
                static nic_attribute_t carrier {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        if (!(nic->flags() & iff_up))
                            return std::unexpected { lib::err::invalid_argument };
                        return fmt::format("{}\n", nic->carrier() ? 1 : 0);
                    }, nullptr, "carrier", 0444
                };
                static nic_attribute_t operstate {
                    [](std::shared_ptr<nic_t> nic) -> lib::expect<std::string> {
                        if (!(nic->flags() & iff_up))
                            return "down\n";
                        return fmt::format("{}\n", nic->carrier() ? "up" : "down");
                    }, nullptr, "operstate", 0444
                };

                static attribute_t *list[] {
                    &address, &broadcast,
                    &ifindex, &iflink,
                    &mtu, &type, &flags,
                    &carrier, &operstate
                };
                return list;
            }

            std::span<const attribute_group_t> groups() const override
            {
                struct stat_attribute_t : dev::make_attribute_t
                {
                    stat_attribute_t(std::atomic_uint64_t stats_t::*member, std::string_view name)
                        : dev::make_attribute_t {
                            [member](dev::device_t &dev) -> lib::expect<std::string> {
                                auto nic = nic_from(dev);
                                if (!nic)
                                    return std::unexpected { lib::err::io_error };
                                return fmt::format("{}\n",
                                    (nic->stats().*member).load(std::memory_order_relaxed)
                                );
                            }, nullptr, name, 0444
                        } { }
                };

#define DEF(name) static stat_attribute_t name { &stats_t::name, #name }
                DEF(rx_packets); DEF(rx_bytes); DEF(rx_errors); DEF(rx_dropped);
                DEF(tx_packets); DEF(tx_bytes); DEF(tx_errors); DEF(tx_dropped);
                DEF(multicast); DEF(collisions);
#undef DEF

                static dev::attribute_t *stats[] {
                    &rx_packets, &rx_bytes, &rx_errors, &rx_dropped,
                    &tx_packets, &tx_bytes, &tx_errors, &tx_dropped,
                    &multicast, &collisions
                };

                static const dev::attribute_group_t list[] {
                    { "statistics", stats }
                };
                return list;
            }
        };

        class loopback_t : public nic_t
        {
            public:
            loopback_t() : nic_t { arphrd::loopback }
            {
                _name = "lo";
                _flags.store(iff_loopback, std::memory_order_relaxed);
            }

            lib::expect<void> do_transmit(std::span<const std::byte> frame) override
            {
                receive(frame);
                return { };
            }

            lib::expect<void> start() override
            {
                set_carrier(true);
                return { };
            }

            void stop() override { }
        };

        lib::initgraph::task register_task
        {
            "dev.net.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require {
                core_registered_stage(),
                lwip::initialised_stage(),
                fs::procfs::registered_stage()
            },
            lib::initgraph::entail { registered_stage() },
            [] {
                lib::bug_on(!register_class(get_class()));

                static auto lo = std::make_shared<loopback_t>();
                lib::bug_on(!register_nic(lo, root("/devices/virtual")));

                using namespace fs::procfs;
                lib::bug_on(!register_per_pid("net",
                    make_dir_ops(),
                    node_type::dir, 0555
                ));

                lib::bug_on(!register_global("net",
                    make_symlink_ops([](auto) {
                        return "self/net";
                    }), node_type::symlink, 0777
                ));

                lib::bug_on(!register_per_pid("net/dev",
                    make_file_ops([](auto) {
                        std::string out {
                            "Inter-|   Receive                                                |"
                                    "  Transmit\n"
                            " face |bytes    packets errs drop fifo frame compressed multicast|"
                                   "bytes    packets errs drop fifo colls carrier compressed\n"
                        };

                        std::vector<std::shared_ptr<nic_t>> snapshot;
                        {
                            const rcu::read_guard _ { };
                            const auto *ptr = nics.dereference();
                            if (!ptr)
                                return out;
                            snapshot = *ptr | std::views::values |
                                std::ranges::to<decltype(snapshot)>();
                        }
                        out.reserve(out.size() + snapshot.size() * 128);

                        auto it = std::back_inserter(out);
                        for (const auto &nic : snapshot)
                        {
                            const auto &stats = nic->stats();
                            fmt::format_to(it,
                                "{:>6}: {:7} {:7} {:4} {:4} {:4} {:5} {:10} {:9} "
                                       "{:7} {:7} {:4} {:4} {:4} {:5} {:7} {:10}\n",
                                nic->name(),
                                stats.rx_bytes.load(std::memory_order_relaxed),
                                stats.rx_packets.load(std::memory_order_relaxed),
                                stats.rx_errors.load(std::memory_order_relaxed),
                                stats.rx_dropped.load(std::memory_order_relaxed),
                                0, //stats.rx_fifo_errors.load(std::memory_order_relaxed),
                                0, //stats.rx_frame_errors.load(std::memory_order_relaxed),
                                0, //stats.rx_compressed.load(std::memory_order_relaxed),
                                stats.multicast.load(std::memory_order_relaxed),
                                stats.tx_bytes.load(std::memory_order_relaxed),
                                stats.tx_packets.load(std::memory_order_relaxed),
                                stats.tx_errors.load(std::memory_order_relaxed),
                                stats.tx_dropped.load(std::memory_order_relaxed),
                                0, // stats.tx_fifo_errors.load(std::memory_order_relaxed),
                                stats.collisions.load(std::memory_order_relaxed),
                                0, // stats.tx_carrier_errors.load(std::memory_order_relaxed),
                                0  // stats.tx_compressed.load(std::memory_order_relaxed)
                            );
                        }
                        return out;
                    }), node_type::file, 0444
                ));

                // TODO: net/route
                // TODO: net/arp
            }
        };
    } // namespace

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "dev.net.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    std::uint32_t nic_t::alloc_idx()
    {
        return next_idx.fetch_add(1, std::memory_order_relaxed);
    }

    void nic_t::receive(std::span<const std::byte> frame)
    {
        auto nif = static_cast<netif *>(lwip.get());
        if (nif == nullptr || frame.size() > std::numeric_limits<std::uint16_t>::max())
        {
            _stats.rx_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto ptr = pbuf_alloc(PBUF_RAW, frame.size(), PBUF_POOL);
        if (!ptr)
        {
            _stats.rx_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        pbuf_take(ptr, frame.data(), frame.size());
        if (nif->input(ptr, nif) != ERR_OK)
        {
            pbuf_free(ptr);
            _stats.rx_dropped.fetch_add(1, std::memory_order_relaxed);
            _stats.rx_errors.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            _stats.rx_packets.fetch_add(1, std::memory_order_relaxed);
            _stats.rx_bytes.fetch_add(frame.size(), std::memory_order_relaxed);
        }
    }

    lib::expect<void> nic_t::transmit(std::span<const std::byte> frame)
    {
        const auto ret = do_transmit(frame);
        if (!ret)
        {
            _stats.tx_dropped.fetch_add(1, std::memory_order_relaxed);
            _stats.tx_errors.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            _stats.tx_packets.fetch_add(1, std::memory_order_relaxed);
            _stats.tx_bytes.fetch_add(frame.size(), std::memory_order_relaxed);
        }
        return ret;
    }

    lib::expect<void> nic_t::set_mtu(std::uint32_t mtu)
    {
        if (mtu == 0 || mtu > std::numeric_limits<std::uint16_t>::max())
            return std::unexpected { lib::err::invalid_argument };

        _mtu.store(mtu, std::memory_order_relaxed);
        if (auto nif = static_cast<netif *>(lwip.get()))
            nif->mtu = mtu;
        return { };
    }

    void nic_t::set_carrier(bool up)
    {
        _carrier.store(up, std::memory_order_relaxed);
        if (auto nif = static_cast<netif *>(lwip.get()))
        {
            if (up)
                netifapi_netif_set_link_up(nif);
            else
                netifapi_netif_set_link_down(nif);
        }
    }

    lib::expect<void> nic_t::up()
    {
        auto nif = static_cast<netif *>(lwip.get());
        if (nif == nullptr)
            return std::unexpected { lib::err::io_error };

        if (const auto ret = lwip::check_err(netifapi_netif_set_up(nif)); !ret)
            return ret;

        if (const auto ret = start(); !ret)
        {
            lib::unused(down());
            return ret;
        }

        _flags.fetch_or(iff_up | iff_running, std::memory_order_relaxed);
        return { };
    }

    lib::expect<void> nic_t::down()
    {
        auto nif = static_cast<netif *>(lwip.get());
        if (nif == nullptr)
            return std::unexpected { lib::err::io_error };

        if (const auto ret = lwip::check_err(netifapi_netif_set_down(nif)); !ret)
            return ret;

        set_carrier(false);
        _flags.fetch_and(~(iff_up | iff_running), std::memory_order_relaxed);
        stop();
        return { };
    }

    mac_t generate_mac()
    {
        mac_t buffer;
        auto span = std::as_writable_bytes(std::span { buffer });
        lib::bug_on(random::get_bytes(span) != std::ssize(buffer));
        // locally administered unicast
        buffer[0] = (buffer[0] & 0xFE) | 0x02;
        return buffer;
    }

    lib::expect<void> register_nic(
        const std::shared_ptr<nic_t> &nic, std::weak_ptr<dev::kobject_t> parent
    )
    {
        // TODO: proper names
        if (nic->_name.empty())
            nic->_name = fmt::format("eth{}", next_ethn.fetch_add(1, std::memory_order_relaxed));

        auto dev = dev::device_t::create(nic->name(), get_ktype(), parent);
        dev->cls = &get_class();
        dev->private_data = std::make_shared<data_t>(nic);
        dev->props.emplace_back("INTERFACE", nic->name());
        dev->props.emplace_back("IFINDEX", std::to_string(nic->index()));

        if (const auto ret = lwip::attach(nic); !ret)
            return ret;

        auto nif = static_cast<netif *>(nic->lwip.get());
        if (nic->carrier())
            netifapi_netif_set_link_up(nif);
        else
            netifapi_netif_set_link_down(nif);

        lib::bug_on(!dev::register_device(dev));
        nic->dev = std::move(dev);

        const std::unique_lock _ { write_lock };
        rcu::updater next { nics };
        lib::bug_on(!next->emplace(nic->index(), nic).second);
        next.commit();
        return { };
    }

    bool unregister_nic(const std::shared_ptr<nic_t> &nic)
    {
        {
            const std::unique_lock _ { write_lock };
            rcu::updater next { nics };
            if (next->erase(nic->index()) == 0)
                return false;
            next.commit();
        }

        lib::unused(nic->down());
        lwip::deattach(nic);

        if (nic->dev)
        {
            lib::bug_on(!dev::unregister_device(nic->dev));
            nic->dev.reset();
        }
        return true;
    }

    std::shared_ptr<nic_t> by_ifindex(std::uint32_t idx)
    {
        const rcu::read_guard _ { };
        if (const auto *ptr = nics.dereference())
        {
            if (auto it = ptr->find(idx); it != ptr->end())
                return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<nic_t> by_name(std::string_view name)
    {
        const rcu::read_guard _ { };
        if (const auto *ptr = nics.dereference())
        {
            const auto pred = [&](const auto &entry) { return entry.second->name() == name; };
            if (const auto found = std::ranges::find_if(*ptr, pred); found != ptr->end())
                return found->second;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<nic_t>> all()
    {
        const rcu::read_guard _ { };
        if (const auto *ptr = nics.dereference())
        {
            return *ptr | std::views::values |
                std::ranges::to<std::vector<std::shared_ptr<nic_t>>>();
        }
        return { };
    }

    class_t &get_class()
    {
        static net_class_t net { };
        return net;
    }

    ktype_t &get_ktype()
    {
        static nic_ktype_t type { };
        return type;
    }
} // namespace dev::net
