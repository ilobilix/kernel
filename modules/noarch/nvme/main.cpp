// Copyright (C) 2024-2026  ilobilo

// some parts of this nvme driver are based on the one from managarm

// TODO: batch submissions and only write to doorbell once
// TODO: interrupt coalescing

import drivers.dev.block;
import drivers.pci;
import system.vfs.dev;
import fmt;
import lib;
import std;

import nvme;

namespace nvme
{
    struct driver_t : pci::driver_t
    {
        static constexpr pci::id_t match_ids[] {
            pci::id_t::from_class(0x01, 0x08, 0x02)
        };

        lib::locker<
            lib::map::flat_hash<
                std::size_t,
                std::shared_ptr<controller_t>
            >, lib::spinlock
        > ctrls;
        std::atomic_size_t idx = 0;

        driver_t() : pci::driver_t { "nvme", match_ids } { }

        ~driver_t()
        {
            lib::bug_on(!dev::unregister_class(get_class()));
        }

        lib::expect<void> probe(pci::device_t &dev) override
        {
            lib::info("nvme: probing device");

            const auto ret = dev::register_class(get_class());
            lib::bug_on(!ret && ret.error() != lib::err::already_exists);

            return controller_t::create(dev.dev).transform([&](auto &&ctrl) {
                const auto id = idx.fetch_add(1, std::memory_order_relaxed);

                auto nvdir = dev::kobject_t::create("nvme", dev::empty_ktype(), dev.as_weak());
                lib::bug_on(!dev::register_kobject(nvdir));

                auto nvctrl = dev::device_t::create(
                    "nvme" + std::to_string(id), get_ctrl_ktype(), nvdir
                );
                nvctrl->cls = &get_class();
                nvctrl->devt = makedev(vfs::dev::alloc_char_major(), id);
                nvctrl->fops = std::make_shared<ctrl_ops_t>(ctrl);
                lib::bug_on(!dev::register_device(nvctrl));

                for (const auto &[nsid, ns] : ctrl->namespaces() | std::views::enumerate)
                {
                    // TODO: nvme specific disk attributes
                    auto dev = dev::device_t::create(
                        fmt::format("nvme{}n{}", id, nsid + 1),
                        dev::block::get_ktype(), nvctrl
                    );
                    dev->cls = &dev::block::get_class();
                    dev->devt = makedev(259, dev::block::alloc_minor());
                    dev->fops = std::make_shared<dev::block::ops_t>(ns);
                    ns->dev = std::move(dev);

                    lib::bug_on(!dev::block::register_drive(ns, "p"));
                }

                ctrl->dev = std::move(nvctrl);
                ctrl->dir = std::move(nvdir);

                lib::bug_on(!ctrls.lock()->emplace(dev.id, std::move(ctrl)).second);
            });
        }

        bool remove(pci::device_t &dev) override
        {
            auto ctrl = [&] -> std::shared_ptr<controller_t> {
                auto locked = ctrls.lock();
                const auto it = locked->find(dev.id);
                if (it == locked->end())
                    return nullptr;

                auto ret = it->second;
                locked->erase(it);
                return ret;
            } ();
            if (!ctrl)
                return false;

            lib::info("nvme: removing device");

            for (const auto &ns : ctrl->namespaces())
                lib::bug_on(!dev::block::unregister_drive(ns));

            lib::bug_on(!dev::unregister_device(ctrl->dev));
            lib::bug_on(!dev::unregister_kobject(ctrl->dir));
            return true;
        }
    } driver;
} // namespace nvme

device_module(
    "nvme", "NVMe block device",
    nvme::driver
);
