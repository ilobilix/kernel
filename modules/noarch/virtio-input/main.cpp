// Copyright (C) 2024-2026  ilobilo

import system.virtio;
import system.input;
import system.dev;
import libarch;
import fmt;
import lib;
import std;

import vinput;

namespace vinput
{
    constexpr std::size_t num_bufs = 64;
    constexpr std::size_t num_status = 16;
    constexpr std::size_t max_batch = 512;

    constexpr std::uint16_t ev_types[] {
        input::ev_key, input::ev_rel, input::ev_abs, input::ev_msc, input::ev_sw,
        input::ev_led, input::ev_snd
    };

    struct state_t
    {
        virtio::device_t &dev;
        std::shared_ptr<input::device_t> idev;

        std::vector<input::value_t> batch;

        std::string name;
        std::string serial;

        virtio::queue_t *equeue;
        virtio::queue_t *squeue;

        lib::spinlock_irq lock;

        arch::contiguous_pool pool;
        arch::dma_array<event_t> status;
        lib::static_bitmap<num_status> status_used;
        bool ready;

        state_t(virtio::device_t &dev)
            : dev { dev }, idev { }, batch { }, name { }, serial { }, equeue { }, squeue { },
              lock { }, pool { }, status { &pool, num_status }, status_used { },
              ready { false } { }

        std::optional<std::size_t> alloc_status()
        {
            return status_used.atomic_view().allocate(0, std::memory_order_acq_rel);
        }

        void free_status(std::size_t idx)
        {
            lib::bug_on(idx >= num_status);
            lib::bug_on(!status_used.atomic_view().set(idx, false, std::memory_order_release));
        }

        std::size_t select_cfg(std::uint8_t select, std::uint8_t subsel)
        {
            dev.write_config<std::uint8_t>(offsetof(config_t, select), select);
            dev.write_config<std::uint8_t>(offsetof(config_t, subsel), subsel);
            return dev.read_config<std::uint8_t>(offsetof(config_t, size));
        }
    };

    lib::locker<
        lib::map::flat_hash<
            std::size_t,
            std::shared_ptr<state_t>
        >, lib::spinlock
    > states;
    struct driver_t : virtio::driver_t
    {
        static constexpr virtio::id_t match_ids[] {
            virtio::id_t::from_type(virtio::device_type::input)
        };

        driver_t() : virtio::driver_t { "virtio-input", match_ids } { }

        lib::expect<void> probe(virtio::device_t &dev) override
        {
            auto state = std::make_shared<state_t>(dev);
            state->batch.reserve(max_batch);

            if (auto ret = input::device_t::create(dev.as_shared()); !ret)
                return std::unexpected { ret.error() };
            else
                state->idev = std::move(*ret);

            auto equeue = dev.setup_rx_queue(
                0, num_bufs, sizeof(event_t),
                [weak = std::weak_ptr { state }](std::span<const std::byte> buffer) {
                    if (buffer.size() < sizeof(event_t))
                        return;

                    const auto state = weak.lock();
                    if (!state)
                        return;

                    auto ev = std::start_lifetime_as<event_t>(buffer.data());
                    state->batch.push_back({ ev->type, ev->code, ev->value });

                    if ((ev->type == input::ev_syn && ev->code == input::syn_report) ||
                        state->batch.size() >= max_batch)
                    {
                        state->idev->report(state->batch);
                        state->batch.clear();
                    }
                }
            );
            if (!equeue)
                return std::unexpected { equeue.error() };
            state->equeue = *equeue;

            auto squeue = dev.setup_queue(
                1, [weak = std::weak_ptr { state }](virtio::cookie_t cookie, std::uint32_t len) {
                    lib::unused(len);

                    if (const auto state = weak.lock())
                        state->free_status(cookie);
                }
            );
            if (!squeue)
                return std::unexpected { squeue.error() };
            state->squeue = *squeue;

            state->idev->on_event =
                [weak = std::weak_ptr { state }](input::device_t &dev, const input::value_t &val) {
                    const auto state = weak.lock();
                    if (!state)
                        return;
                    lib::bug_on(state->idev.get() != std::addressof(dev));

                    if (val.type == input::ev_msc && val.code == input::msc_timestamp)
                        return;

                    const std::unique_lock _ { state->lock };
                    if (!state->ready)
                        return;

                    const auto idx = state->alloc_status();
                    if (!idx)
                        return;

                    auto &slot = state->status[*idx];
                    slot.type = val.type;
                    slot.code = val.code;
                    slot.value = val.value;

                    const virtio::buffer_t buf[] { {
                        lib::fromhh(reinterpret_cast<std::uintptr_t>(std::addressof(slot))),
                        static_cast<std::uint32_t>(sizeof(event_t))
                    } };

                    if (const auto ret = state->squeue->add(buf, { }, *idx); !ret)
                    {
                        state->free_status(*idx);
                        return;
                    }
                    state->squeue->submit();
                };

            const auto read_string = [&](std::uint8_t select) {
                const auto size = state->select_cfg(select, 0);
                decltype(config_t::info.string) string;
                dev.read_config(
                    offsetof(config_t, info.string),
                    std::as_writable_bytes(std::span { string })
                );
                return std::string { lib::trim({ string, std::min(size, sizeof(string)) }) };
            };

            state->name = read_string(cfg_id_name);
            state->serial = read_string(cfg_id_serial);

            state->idev->desc = state->name;
            state->idev->uniq = state->serial;
            state->idev->phys = fmt::format("{}/input0", dev.name);

            {
                const auto size = state->select_cfg(cfg_id_devids, 0);
                if (size >= sizeof(devids_t))
                {
                    const auto id = dev.read_config<devids_t>(offsetof(config_t, info.ids));
                    std::memcpy(&state->idev->ident, &id, sizeof(id));
                }
                else state->idev->ident.bustype = input::bus_virtual;
            }

            const auto set_bits = [&](
                std::uint8_t select, std::uint8_t subsel, std::size_t bitcount,
                std::function_ref<void (std::size_t)> fn
            ) {
                const auto bytes = state->select_cfg(select, subsel);
                if (!bytes)
                    return 0uz;

                bitcount = std::min(bitcount, bytes * 8);
                lib::u8buffer buffer { bytes };
                dev.read_config(
                    offsetof(config_t, info.bitmap),
                    std::as_writable_bytes(buffer.span())
                );
                lib::bitmap bmap { buffer.data(), bitcount };

                for (std::size_t i = 0; i < bitcount; i++)
                {
                    if (bmap[i])
                        fn(i);
                }
                return bytes;
            };

            set_bits(cfg_prop_bits, 0, input::prop_cnt, [&](std::size_t i) {
                state->idev->set_prop(i);
            });

            if (state->select_cfg(cfg_ev_bits, input::ev_rep))
                state->idev->events.set(input::ev_rep, true);

            for (const auto type : ev_types)
            {
                const auto bytes = set_bits(
                    cfg_ev_bits, type, input::code_count(type),
                    [&](std::size_t code) {
                        state->idev->set_cap(type, code);
                    }
                );
                if (bytes)
                    state->idev->events.set(type, true);
            }

            if (state->idev->events.get(input::ev_abs))
            {
                const auto read_abs = [&](std::uint16_t axis) -> std::optional<input::absinfo_t> {
                    if (state->select_cfg(cfg_abs_info, axis) < sizeof(absinfo_t))
                        return std::nullopt;

                    const auto info = dev.read_config<absinfo_t>(offsetof(config_t, info.abs));
                    return input::absinfo_t {
                        .value = 0,
                        .minimum = static_cast<std::int32_t>(info.min),
                        .maximum = static_cast<std::int32_t>(info.max),
                        .fuzz = static_cast<std::int32_t>(info.fuzz),
                        .flat = static_cast<std::int32_t>(info.flat),
                        .resolution = static_cast<std::int32_t>(info.res)
                    };
                };

                const auto bits = state->idev->supported.bits(input::ev_abs);
                if (bits.get(input::abs_mt_slot))
                {
                    const auto info = read_abs(input::abs_mt_slot);
                    if (info && info->maximum >= 0)
                        state->idev->set_mt_slots(info->maximum + 1);
                }

                for (std::size_t i = 0; i < input::abs_cnt; i++)
                {
                    if (!bits.get(i))
                        continue;

                    if (const auto info = read_abs(i))
                        state->idev->set_abs(i, *info);
                }
            }

            if (const auto ret = state->idev->register_device(); !ret)
            {
                lib::error(
                    "virtio-input: could not register device: {}",
                    lib::error_name(ret.error())
                );
                return std::unexpected { ret.error() };
            }

            lib::bug_on(!states.lock()->emplace(dev.id, state).second);
            const std::unique_lock _ { state->lock };
            state->ready = true;
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

            {
                const std::unique_lock _ { state->lock };
                state->ready = false;
            }
            state->idev->unregister_device();
            return true;
        }
    } driver;
} // namespace vinput

device_module(
    "virtio-input", "Virtual human interface devices",
    vinput::driver, "virtio-pci"
);
