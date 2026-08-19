// Copyright (C) 2024-2026  ilobilo

import drivers.virtio;
import drivers.dev;
import system.random;
import system.sched;
import libarch;
import lib;
import std;

namespace rng
{
    constexpr std::size_t request_size = 64;
    constexpr std::uint64_t period_ms = 10'000;

    struct driver_t : virtio::driver_t
    {
        static constexpr virtio::id_t match_ids[] {
            virtio::id_t::from_type(virtio::device_type::entropy_source)
        };

        struct device_t
        {
            arch::contiguous_pool pool;
            arch::dma_buffer buffer;
            virtio::queue_t *queue;

            lib::spinlock lock;
            bool stopped;

            device_t()
                : pool { }, buffer { &pool, request_size }, queue { }, lock { },
                  stopped { false } { }

            std::uint8_t *data() const
            {
                return static_cast<std::uint8_t *>(buffer.data());
            }

            lib::expect<void> submit()
            {
                const virtio::buffer_t buf[] {
                    { lib::fromhh(reinterpret_cast<std::uintptr_t>(data())), request_size }
                };

                if (const auto ret = queue->add({ }, buf, 0); !ret)
                    return ret;

                queue->submit();
                return { };
            }
        };

        lib::locker<
            lib::map::flat_hash<
                std::size_t,
                std::shared_ptr<device_t>
            >, lib::spinlock
        > devices;

        static void rearm(std::weak_ptr<device_t> weak)
        {
            sched::schedule_work_after_ns(
                [weak = std::move(weak)] {
                    const auto device = weak.lock();
                    if (!device)
                        return;

                    const std::unique_lock _ { device->lock };
                    if (device->stopped)
                        return;

                    if (const auto ret = device->submit(); !ret)
                    {
                        lib::error(
                            "virtio-rng: could not submit request: {}",
                            lib::error_name(ret.error())
                        );
                    }
                }, period_ms * 1'000'000
            );
        }

        driver_t() : virtio::driver_t { "virtio-rng", match_ids } { }

        lib::expect<void> probe(virtio::device_t &dev) override
        {
            auto device = std::make_shared<device_t>();
            if (device->data() == nullptr)
                return std::unexpected { lib::err::out_of_memory };

            auto queue = dev.setup_queue(
                0, [weak = std::weak_ptr { device }](virtio::cookie_t cookie, std::uint32_t len) {
                    lib::unused(cookie);

                    const auto device = weak.lock();
                    if (!device)
                        return;

                    const auto got = std::min<std::size_t>(len, request_size);
                    if (got != 0)
                        random::add_entropy(std::as_bytes(std::span { device->data(), got }));

                    rearm(weak);
                }
            );
            if (!queue)
                return std::unexpected { queue.error() };

            device->queue = *queue;
            dev.set_ready();

            if (const auto ret = device->submit(); !ret)
                return std::unexpected { ret.error() };

            lib::bug_on(!devices.lock()->emplace(dev.id, std::move(device)).second);
            return { };
        }

        bool remove(virtio::device_t &dev) override
        {
            auto device = [&] -> std::shared_ptr<device_t> {
                auto locked = devices.lock();
                const auto it = locked->find(dev.id);
                if (it == locked->end())
                    return nullptr;

                auto ret = std::move(it->second);
                locked->erase(it);
                return ret;
            } ();
            if (!device)
                return false;

            const std::unique_lock _ { device->lock };
            device->stopped = true;
            return true;
        }
    } driver;
} // namespace rng

device_module(
    "virtio-rng", "High-quality randomness source",
    rng::driver, "virtio-pci"
);
