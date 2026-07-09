// Copyright (C) 2024-2026  ilobilo

// some parts of this nvme driver are based on the one from managarm

// TODO: batch submissions and only write to doorbell once
// TODO: interrupt coalescing

import system.dev;
import system.dev.block;
import system.vfs.dev;
import system.pci;
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
            std::shared_ptr<controller_t>
        > ctrls;
        std::size_t idx = 0;

        driver_t() : pci::driver_t { "nvme", ids }
        {
            lib::bug_on(!dev::register_class(get_class()));
        }

        ~driver_t()
        {
            lib::bug_on(!dev::unregister_class(get_class()));
        }

        lib::expect<void> probe(pci::device_t &dev) override
        {
            lib::info("nvme: probing device");

            return controller_t::create(dev.dev).transform([&](auto &&ctrl) {
                auto nvdir = dev::kobject_t::create("nvme", dev::empty_ktype(), dev.as_weak());
                lib::bug_on(!dev::register_kobject(nvdir));

                auto nvctrl = dev::device_t::create(
                    "nvme" + std::to_string(idx), get_ctrl_ktype(), nvdir
                );
                nvctrl->cls = &get_class();
                nvctrl->devt = vfs::dev::makedev(vfs::dev::alloc_char_major(), idx);
                nvctrl->fops = std::make_shared<ctrl_ops_t>(ctrl);
                lib::bug_on(!dev::register_device(nvctrl));

                for (const auto &[nsid, ns] : ctrl->namespaces() | std::views::enumerate)
                {
                    auto dev = dev::device_t::create(
                        "nvme0n" + std::to_string(nsid + 1), get_ns_ktype(), nvctrl
                    );
                    dev->cls = &dev::block::get_class();
                    dev->devt = vfs::dev::makedev(259, dev::block::alloc_minor());
                    dev->fops = std::make_shared<dev::block::ops_t>(ns);
                    ns->dev = std::move(dev);

                    lib::bug_on(!dev::block::register_drive(ns, "p"));
                }

                ctrl->dev = std::move(nvctrl);
                ctrl->dir = std::move(nvdir);

                idx++;
                lib::bug_on(!ctrls.emplace(dev.id, std::move(ctrl)).second);
            });
        }

        bool remove(pci::device_t &dev) override
        {
            if (auto it = ctrls.find(dev.id); it != ctrls.end())
            {
                lib::info("nvme: removing device");

                auto &[_, ctrl] = *it;

                for (const auto &ns : ctrl->namespaces())
                    lib::bug_on(!dev::block::unregister_drive(ns));

                lib::bug_on(!dev::unregister_device(ctrl->dev));
                lib::bug_on(!dev::unregister_kobject(ctrl->dir));

                ctrls.erase(it);
                return true;
            }
            return false;
        }
    } driver;
} // namespace nvme

pci_module(
    "nvme", "NVMe block device",
    nvme::driver, nvme::ids
);
