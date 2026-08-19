// Copyright (C) 2024-2026  ilobilo

export module vnet;

export import :spec;

import system.sched.wait_queue;
import system.sched.mutex;
import drivers.dev.net;
import drivers.virtio;
import libarch;
import lib;
import std;

export namespace vnet
{
    namespace net = dev::net;

    class device_t : public dev::net::nic_t
    {
        private:
        struct queue_pair_t
        {
            arch::contiguous_pool *pool;

            virtio::queue_t *rx;
            virtio::queue_t *tx;

            lib::spinlock lock;
            virtio::cookie_t next_cookie;
            lib::map::flat_hash<
                virtio::cookie_t,
                arch::dma_buffer
            > tx_bufs;

            lib::expect<std::pair<virtio::cookie_t, arch::dma_buffer_view>> alloc(std::size_t size);
            void free(virtio::cookie_t cookie);

            queue_pair_t(arch::contiguous_pool *pool, virtio::queue_t *rx, virtio::queue_t *tx)
                : pool { pool }, rx { rx }, tx { tx }, lock { }, next_cookie { 0 }, tx_bufs { } { }
        };

        virtio::device_t &_dev;

        arch::contiguous_pool pool;

        lib::buffer<queue_pair_t> queues;
        std::size_t nqueues = 0;
        std::size_t used_pairs = 1;
        std::uint16_t max_pairs = 1;

        virtio::queue_t *ctrlq = nullptr;

        sched::wait_queue_t ctrl_wait;
        sched::mutex_t ctrl_lock;

        std::atomic_bool running = false;

        // bool guest_gso = false;

        bool has_any_feat(feature feat) const
        {
            return _dev.has_any(static_cast<std::uint64_t>(feat));
        }

        std::size_t header_size() const;
        bool link_up();

        lib::expect<void> ctrl_cmd(
            ctrl_class cls, std::uint8_t cmd, std::span<const std::byte> data
        );

        lib::expect<void> setup_queues();
        lib::expect<void> init();

        device_t(virtio::device_t &dev) : _dev { dev } { }

        public:
        static lib::expect<std::shared_ptr<device_t>> create(virtio::device_t &dev);

        lib::expect<void> do_transmit(std::span<const std::byte> frame) override;

        lib::expect<void> start() override;
        void stop() override;

        lib::expect<void> set_mtu(std::uint32_t mtu) override;
        lib::expect<void> set_mac(const net::mac_t &mac) override;

        ~device_t();
    };
} // export namespace vnet
