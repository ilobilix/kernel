// Copyright (C) 2024-2025  ilobilo

module system.vfs.dev;

import drivers.initramfs;
import system.vfs;
import magic_enum;
import lib;
import std;

namespace vfs::dev
{
    namespace
    {
        lib::locker<
            lib::map::flat_hash<
                dev_t,
                std::shared_ptr<vfs::ops>
            >, lib::rwspinlock
        > cdev_table;
    } // namespace

    bool register_cdev(std::shared_ptr<vfs::ops> ops, dev_t dev)
    {
        auto [it, inserted] = cdev_table.write_lock()->emplace(dev, ops);
        if (inserted)
            lib::debug("dev: registered character device ({}, {})", major(dev), minor(dev));
        return inserted;
    }

    std::shared_ptr<vfs::ops> get_cdev_ops(dev_t dev)
    {
        const auto lock = cdev_table.read_lock();
        const auto it = lock->find(dev);
        if (it == lock->end())
            return nullptr;
        return it->second;
    }
} // namespace vfs::dev