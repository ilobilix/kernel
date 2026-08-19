// Copyright (C) 2024-2026  ilobilo

module vnet;

import system.cpu.local;
import system.cpu;
import fmt;

namespace vnet
{
    namespace
    {
        constexpr std::size_t eth_hdr_size = 14;
        constexpr std::size_t max_rx_bufs = 64;
    } // namespace

    auto device_t::queue_pair_t::alloc(std::size_t size)
        -> lib::expect<std::pair<virtio::cookie_t, arch::dma_buffer_view>>
    {
        arch::dma_buffer buf { pool, size };
        if (buf.data() == nullptr)
            return std::unexpected { lib::err::out_of_memory };

        const std::unique_lock _ { lock };
        const auto cookie = next_cookie++;
        const auto res = tx_bufs.emplace(cookie, std::move(buf));
        lib::bug_on(!res.second);
        return std::pair<virtio::cookie_t, arch::dma_buffer_view> { cookie, res.first->second };
    }

    void device_t::queue_pair_t::free(virtio::cookie_t cookie)
    {
        const std::unique_lock _ { lock };
        lib::bug_on(!tx_bufs.erase(cookie));
    }

    std::size_t device_t::header_size() const
    {
        std::size_t hdr_size = sizeof(net_hdr_t);
        if (has_any_feat(feature::mrg_rxbuf) || _dev.has(virtio::version_1))
            hdr_size += 2;
        return hdr_size;
    }

    bool device_t::link_up()
    {
        if (!has_any_feat(feature::status))
            return true;

        const auto stat = _dev.read_config_stable<&net_config_t::status>();
        return (stat & status::link_up) != status::none;
    }

    lib::expect<void> device_t::ctrl_cmd(
        ctrl_class cls, std::uint8_t cmd, std::span<const std::byte> payload
    )
    {
        if (!has_any_feat(feature::ctrl_vq) || ctrlq == nullptr)
            return std::unexpected { lib::err::not_supported };

        const std::uint32_t req_size = sizeof(net_ctrl_t) + payload.size();

        const std::unique_lock _ { ctrl_lock };

        arch::dma_buffer buffer { &pool, req_size + sizeof(ctrl_ack) };
        if (buffer.data() == nullptr)
            return std::unexpected { lib::err::out_of_memory };

        auto ctrl = std::start_lifetime_as<net_ctrl_t>(buffer.data());
        ctrl->class_ = cls;
        ctrl->command = cmd;
        std::memcpy(ctrl->data, payload.data(), payload.size());

        auto ack = std::start_lifetime_as<ctrl_ack>(buffer.byte_data() + req_size);
        *ack = ctrl_ack::err;

        const auto phys = lib::fromhh(reinterpret_cast<std::uintptr_t>(buffer.data()));

        const virtio::buffer_t cmd_buf[] {
            { phys, req_size }
        };
        const virtio::buffer_t ack_buf[] {
            { phys + req_size, sizeof(ctrl_ack) }
        };

        if (const auto ret = ctrlq->add(cmd_buf, ack_buf, 0); !ret)
            return ret;

        const auto gen = ctrl_wait.snapshot_gen();
        ctrlq->submit();
        ctrl_wait.wait_unkillable_prepared(gen);

        if (*ack != ctrl_ack::ok)
            return std::unexpected { lib::err::io_error };
        return { };
    }

    lib::expect<void> device_t::setup_queues()
    {
        std::size_t queue_pairs = 1;
        if (has_any_feat(feature::mq))
        {
            max_pairs = _dev.read_config<&net_config_t::max_virtqueue_pairs>();
            if (max_pairs < mq_pairs_min || max_pairs > mq_pairs_max)
                return std::unexpected { lib::err::invalid_argument };
            queue_pairs = std::min<std::size_t>(max_pairs, cpu::count());
        }

        if (has_any_feat(feature::mtu))
        {
            const std::uint16_t mtu = _dev.read_config<&net_config_t::mtu>();
            if (mtu < 68)
                return std::unexpected { lib::err::invalid_argument };
            _mtu.store(mtu, std::memory_order_relaxed);
        }

        std::size_t rxsize = 0x1000;
        using enum feature;
        if (has_any_feat(guest_tso4 | guest_tso6 | guest_ufo) ||
            (has_any_feat(guest_uso4) && has_any_feat(guest_uso6)))
        {
            rxsize = lib::align_up(65562uz, 0x1000);
            // guest_gso = true;
        }
        rxsize = std::max(rxsize, nic_t::mtu() + eth_hdr_size + header_size());

        lib::info("virtio-net: {} queue pair(s). rx queue size: 0x{:X} bytes", queue_pairs, rxsize);

        queues.allocate(queue_pairs);
        for (std::size_t i = 0; i < queue_pairs; i++)
        {
            auto ret = _dev.setup_rx_queue(
                i * 2, max_rx_bufs, rxsize, [this](std::span<const std::byte> buffer) {
                    if (!running.load(std::memory_order_acquire))
                        return;

                    const auto hdr_size = header_size();
                    if (buffer.size() < hdr_size)
                        return;
                    receive(buffer.subspan(hdr_size));
                }
            );
            if (!ret)
                return std::unexpected { ret.error() };
            auto rx = *ret;

            ret = _dev.setup_queue(i * 2 + 1, [this, i](virtio::cookie_t cookie, std::uint32_t) {
                queues[i].free(cookie);
            });
            if (!ret)
                return std::unexpected { ret.error() };

            std::construct_at(queues.data() + i, &pool, rx, *ret);
            nqueues++;
        }

        if (has_any_feat(feature::ctrl_vq))
        {
            lib::info("virtio-net: control queue supported");

            const auto ret = _dev.setup_queue(max_pairs * 2, [this](auto, auto) {
                ctrl_wait.wake_all();
            });
            if (!ret)
                return std::unexpected { ret.error() };
            ctrlq = *ret;
        }

        return { };
    }

    lib::expect<void> device_t::init()
    {
        if (const auto ret = setup_queues(); !ret)
            return ret;

        _dev.on_config_change([this] { set_carrier(link_up()); });

        std::optional<net::mac_t> generated;
        if (has_any_feat(feature::mac))
            _mac = _dev.read_config<&net_config_t::mac>();
        else
            _mac = *(generated = dev::net::generate_mac());

        _dev.set_ready();
        if (generated)
        {
            if (const auto ret = set_mac(*generated); !ret)
            {
                lib::warn(
                    "virtio-net: could not set generated mac address: {}",
                    lib::error_name(ret.error())
                );
            }
        }

        if (nqueues > 1)
        {
            const std::uint16_t pairs = nqueues;
            const auto ret = ctrl_cmd(
                ctrl_class::mq, std::to_underlying(ctrl_mq::vq_pairs_set),
                std::as_bytes(std::span { &pairs, 1 })
            );
            if (!ret)
            {
                lib::warn(
                    "virtio-net: could not enable {} queue pairs: {}",
                    pairs, lib::error_name(ret.error())
                );
            }
            else used_pairs = nqueues;
        }

        lib::info("virtio-net: mac address: {:02X}", fmt::join(_mac, ":"));
        return { };
    }

    device_t::~device_t()
    {
        for (std::size_t i = 0; i < nqueues; i++)
            std::destroy_at(queues.data() + i);
    }

    lib::expect<std::shared_ptr<device_t>> device_t::create(virtio::device_t &dev)
    {
        auto device = std::shared_ptr<device_t> { new device_t { dev } };
        if (const auto ret = device->init(); !ret)
            return std::unexpected { ret.error() };
        return device;
    }

    lib::expect<void> device_t::do_transmit(std::span<const std::byte> frame)
    {
        if (!running.load(std::memory_order_acquire))
            return std::unexpected { lib::err::network_down };

        const auto hdr_size = header_size();
        auto &queue = queues[cpu::self().unsafe_get().idx % used_pairs];

        auto alloc = queue.alloc(hdr_size + frame.size());
        if (!alloc)
            return std::unexpected { alloc.error() };

        auto [cookie, buf_view] = *alloc;
        // auto hdr = std::start_lifetime_as<net_hdr_t>(buf_view.data());
        // if (has_any_feat(feature::csum))
        // {
        //     hdr->flags = flag::needs_csum;
        //     TODO
        // }
        std::memset(buf_view.byte_data(), 0, hdr_size);
        std::memcpy(buf_view.byte_data() + hdr_size, frame.data(), frame.size());

        const bool split = !frame.empty() &&
            !_dev.has(virtio::version_1) && !_dev.has(virtio::any_layout);

        const auto phys = lib::fromhh(reinterpret_cast<std::uintptr_t>(buf_view.data()));
        const virtio::buffer_t out[] {
            { phys, static_cast<std::uint32_t>(split ? hdr_size : buf_view.size()) },
            { phys + hdr_size, static_cast<std::uint32_t>(frame.size()) }
        };

        const auto ret = queue.tx->add(std::span { out } .first(split ? 2 : 1), { }, cookie);
        if (!ret)
        {
            queue.free(cookie);
            return ret;
        }

        queue.tx->submit();
        return { };
    }

    lib::expect<void> device_t::start()
    {
        if (running.exchange(true, std::memory_order_acq_rel))
            return { };

        set_carrier(link_up());
        return { };
    }

    void device_t::stop()
    {
        if (!running.exchange(false, std::memory_order_acq_rel))
            return;

        set_carrier(false);
    }

    lib::expect<void> device_t::set_mtu(std::uint32_t mtu)
    {
        lib::unused(mtu);
        return std::unexpected { lib::err::not_supported };
    }

    lib::expect<void> device_t::set_mac(const net::mac_t &mac)
    {
        if (has_any_feat(feature::ctrl_mac_addr))
        {
            const auto ret = ctrl_cmd(
                ctrl_class::mac, std::to_underlying(ctrl_mac::addr_set),
                std::as_bytes(std::span { mac })
            );
            if (!ret)
            {
                lib::error(
                    "virtio-net: could not change mac address: {}",
                    lib::error_name(ret.error())
                );
                return ret;
            }
        }
        else if (has_any_feat(feature::mac) && !_dev.has(virtio::version_1))
            _dev.write_config<&net_config_t::mac>(mac);
        else
            return std::unexpected { lib::err::not_supported };

        _mac = mac;
        return { };
    }
} // namespace vnet
