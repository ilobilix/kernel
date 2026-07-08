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

            const auto minor = [](std::uint32_t ctrl, std::uint32_t ns) {
                return (ctrl * 1048576) + (ns * 256);
            };

            return controller_t::create(dev).transform([&](auto &&ctrl) {
                auto nvdir = dev::kobject_t::create("nvme", dev::empty_ktype(), dev.as_weak());
                lib::bug_on(!dev::register_kobject(nvdir));

                auto nvctrl = dev::device_t::create(
                    "nvme" + std::to_string(idx), get_ctrl_ktype(), nvdir
                );
                nvctrl->cls = &get_class();
                nvctrl->devt = vfs::dev::makedev(dev::block::alloc_major(), minor(idx, 0));
                nvctrl->fops = std::make_shared<ctrl_ops_t>(ctrl);
                lib::bug_on(!dev::register_device(nvctrl));

                for (const auto &[idx, ns] : ctrl->namespaces() | std::views::enumerate)
                {
                    auto dev = dev::device_t::create(
                        "nvme0n" + std::to_string(idx + 1), get_ns_ktype(), nvctrl
                    );
                    dev->cls = &dev::block::get_class();
                    dev->devt = vfs::dev::makedev(259, idx);
                    dev->fops = std::make_shared<ns_ops_t>(ns);
                    lib::bug_on(!dev::register_device(dev));
                }

                // TODO: scan partitions

                idx++;
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
