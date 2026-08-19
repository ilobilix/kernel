// Copyright (C) 2024-2026  ilobilo

import drivers.virtio;
import drivers.dev.net;
import magic_enum;
import lib;
import std;

import vnet;

namespace vnet
{
    struct driver_t : virtio::driver_t
    {
        static constexpr virtio::id_t match_ids[] {
            virtio::id_t::from_type(virtio::device_type::network)
        };

        lib::locker<
            lib::map::flat_hash<
                std::size_t,
                std::shared_ptr<device_t>
            >, lib::spinlock
        > devices;

        driver_t() : virtio::driver_t { "virtio-net", match_ids } { }

        std::uint64_t features() const override
        {
            using namespace magic_enum::bitwise_operators;
            using enum feature;
            return std::to_underlying(
                mac | mtu | status |
                ctrl_vq | mq | mrg_rxbuf | ctrl_mac_addr
                // TODO
                // csum | guest_csum |
                // guest_tso4 | guest_tso6 | guest_ufo | guest_uso4 | guest_uso6
            );
        }

        lib::expect<void> probe(virtio::device_t &dev) override
        {
            lib::info("virtio-net: probing device");

            return device_t::create(dev).transform([&](auto &&nic) {
                lib::bug_on(!dev::net::register_nic(nic, dev.as_weak()));
                lib::bug_on(!devices.lock()->emplace(dev.id, std::move(nic)).second);
            });
        }

        bool remove(virtio::device_t &dev) override
        {
            auto nic = [&] -> std::shared_ptr<device_t> {
                auto locked = devices.lock();
                const auto it = locked->find(dev.id);
                if (it == locked->end())
                    return nullptr;

                auto ret = it->second;
                locked->erase(it);
                return ret;
            } ();
            if (!nic)
                return false;

            lib::info("virtio-net: removing device");

            lib::bug_on(!dev::net::unregister_nic(nic));
            return true;
        }
    } driver;
} // namespace vnet

device_module(
    "virtio-net", "Virtual network interface controller",
    vnet::driver, "virtio-pci"
);
