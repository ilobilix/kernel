// Copyright (C) 2024-2026  ilobilo

import system.virtio;
import system.random;
import system.sched;
import system.dev;
import libarch;
import lib;
import std;

namespace rng
{
    constexpr std::size_t request_size = 64;
    constexpr std::uint64_t period_ms = 10'000;

    struct state_t
    {
        arch::contiguous_pool pool;
        arch::dma_buffer_view buffer;
        virtio::queue_t *queue;

        lib::spinlock lock;
        bool stopped;

        state_t()
            : pool { }, buffer { pool.allocate(request_size, 1, request_size), request_size },
              queue { }, lock { }, stopped { false } { }

        ~state_t()
        {
            if (buffer.size() != 0)
                pool.deallocate(buffer.get_dma_ptr(), buffer.size(), 1, request_size);
        }

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
            std::shared_ptr<state_t>
        >, lib::spinlock
    > states;

    void rearm(std::weak_ptr<state_t> weak)
    {
        sched::schedule_work_after_ns(
            [weak = std::move(weak)] {
                const auto state = weak.lock();
                if (!state)
                    return;

                const std::unique_lock _ { state->lock };
                if (state->stopped)
                    return;

                if (const auto ret = state->submit(); !ret)
                {
                    lib::error(
                        "virtio-rng: could not submit request: {}",
                        lib::error_name(ret.error())
                    );
                }
            }, period_ms * 1'000'000
        );
    }

    struct driver_t : virtio::driver_t
    {
        static constexpr virtio::id_t match_ids[] {
            virtio::id_t::from_type(virtio::device_type::entropy_source)
        };

        driver_t() : virtio::driver_t { "virtio-rng", match_ids } { }

        lib::expect<void> probe(virtio::device_t &dev) override
        {
            auto state = std::make_shared<state_t>();
            if (state->data() == nullptr)
                return std::unexpected { lib::err::out_of_memory };

            auto queue = dev.setup_queue(
                0, [weak = std::weak_ptr { state }](virtio::cookie_t, std::uint32_t len) {
                    const auto state = weak.lock();
                    if (!state)
                        return;

                    const auto got = std::min<std::size_t>(len, request_size);
                    if (got != 0)
                        random::add_entropy(std::as_bytes(std::span { state->data(), got }));

                    rearm(weak);
                }
            );
            if (!queue)
                return std::unexpected { queue.error() };

            state->queue = *queue;
            dev.set_ready();

            if (const auto ret = state->submit(); !ret)
                return std::unexpected { ret.error() };

            lib::bug_on(!states.lock()->emplace(dev.id, std::move(state)).second);
            return { };
        }

        bool remove(virtio::device_t &dev) override
        {
            auto state = [&] -> std::shared_ptr<state_t> {
                auto locked = states.lock();
                const auto it = locked->find(dev.id);
                if (it == locked->end())
                    return nullptr;

                auto ret = std::move(it->second);
                locked->erase(it);
                return ret;
            } ();
            if (!state)
                return false;

            const std::unique_lock _ { state->lock };
            state->stopped = true;
            return true;
        }
    } driver;
} // namespace rng

device_module(
    "virtio-rng", "High-quality randomness source",
    rng::driver, "virtio-pci"
);
