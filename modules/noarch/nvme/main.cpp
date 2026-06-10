// Copyright (C) 2024-2026  ilobilo

// some parts of this nvme driver are based on the one from managarm

// TODO: batch submissions and only write to doorbell once
// TODO: interrupt coalescing

import system.pci;
import system.dev;
import lib;
import std;

import nvme;

namespace nvme
{
    constexpr pci::id_t ids[] {
        { 0xFFFF, 0xFFFF, 0xFFFFFFFF, pci::id_t::make_class(0x01, 0x08, 0x02) }
    };

    struct driver_t : pci::driver_t
    {
        lib::map::flat_hash<
            std::size_t,
            std::unique_ptr<controller_t>
        > ctrls;

        driver_t() : pci::driver_t { "nvme", ids } { }

        lib::expect<void> probe(pci::device_t &dev) override
        {
            lib::info("nvme: probing device");

            return controller_t::create(dev).transform([&](auto &&ctrl) {
                lib::bug_on(!ctrls.emplace(dev.id, std::move(ctrl)).second);
            });
        }

        bool remove(pci::device_t &dev) override
        {
            const auto ret = ctrls.erase(dev.id);
            if (ret)
                lib::info("nvme: removed device");
            return ret;
        }
    } driver;
} // namespace nvme

pci_module(
    "nvme", "NVMe block device",
    nvme::driver, nvme::ids
);
