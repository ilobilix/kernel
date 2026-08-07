// Copyright (C) 2024-2026  ilobilo

import system.virtio;
import system.dev;
import lib;

namespace rng
{
    constexpr virtio::id_t ids[] {
        virtio::id_t::from_type(virtio::device_type::entropy_source)
    };

    struct driver_t : virtio::driver_t
    {
        driver_t() : virtio::driver_t { "virtio-rng", ids } { }

        lib::expect<void> probe(virtio::device_t &dev) override
        {
            lib::unused(dev);
            lib::info("probing virtio-rng");
            // TODO
            return { };
        }

        bool remove(virtio::device_t &dev) override
        {
            lib::unused(dev);
            lib::info("removing virtio-rng");
            // TODO
            return true;
        }
    } driver;
} // namespace rng

device_module(
    "virtio-rng", "High-quality randomness source",
    rng::driver, rng::ids, "virtio-pci"
);
