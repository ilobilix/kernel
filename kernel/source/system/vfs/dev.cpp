// Copyright (C) 2024-2026  ilobilo

module system.vfs.dev;

import drivers.initramfs;
import system.vfs.pipe;
import system.vfs.socket;

namespace vfs::dev
{
    namespace
    {
        // clang-format off
        lib::locker<
            lib::map::flat_hash<
                dev_t,
                std::shared_ptr<vfs::ops>
            >, lib::rwspinlock
        > cdev_table;
        // clang-format on

        std::shared_ptr<vfs::ops> get_cdev_ops(dev_t rdev)
        {
            const auto lock = cdev_table.read_lock();
            const auto it = lock->find(rdev);
            if (it == lock->end())
                return nullptr;
            return it->second;
        }
    } // namespace

    bool register_ops(dev_t rdev, std::shared_ptr<vfs::ops> ops)
    {
        auto [it, inserted] = cdev_table.write_lock()->emplace(rdev, ops);
        if (inserted)
            lib::debug("dev: registered ops for device ({}, {})", major(rdev), minor(rdev));
        return inserted;
    }

    bool unregister_ops(dev_t rdev)
    {
        auto lock = cdev_table.write_lock();
        const auto it = lock->find(rdev);
        if (it == lock->end())
            return false;

        lock->erase(it);
        lib::debug("dev: unregistered ops for device ({}, {})", major(rdev), minor(rdev));
        return true;
    }

    auto get_ops(dev_t rdev, mode_t mode) -> lib::expect<std::shared_ptr<vfs::ops>>
    {
        switch (stat::type(mode))
        {
            case stat::type::s_ifchr:
            {
                if (rdev == 0)
                    return std::unexpected { lib::err::invalid_device_or_address };
                auto ops = get_cdev_ops(rdev);
                if (!ops)
                    return std::unexpected { lib::err::invalid_device_or_address };
                return ops;
            }
            case stat::type::s_ifblk:
                return std::unexpected { lib::err::todo };
            default:
                return std::unexpected { lib::err::invalid_device_or_address };
        }
        std::unreachable();
    }
} // namespace vfs::dev
