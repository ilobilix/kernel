// Copyright (C) 2024-2026  ilobilo

module system.vfs.dev;

import drivers.initramfs;
import system.vfs.pipe;
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

        lib::locker<
            lib::map::flat_hash<
                dev_t,
                std::shared_ptr<vfs::ops>
            >, lib::rwspinlock
        > fs_ops_table;

        std::shared_ptr<vfs::ops> get_dev_ops(dev_t rdev)
        {
            const auto lock = cdev_table.read_lock();
            const auto it = lock->find(rdev);
            if (it == lock->end())
                return nullptr;
            return it->second;
        }

        std::shared_ptr<vfs::ops> get_fs_ops(dev_t dev)
        {
            const auto lock = fs_ops_table.read_lock();
            const auto it = lock->find(dev);
            if (it == lock->end())
                return nullptr;
            return it->second;
        }
    } // namespace

    bool register_dev_ops(dev_t rdev, std::shared_ptr<vfs::ops> ops)
    {
        auto [it, inserted] = cdev_table.write_lock()->emplace(rdev, ops);
        if (inserted)
            lib::debug("dev: registered ops for device ({}, {})", major(rdev), minor(rdev));
        return inserted;
    }

    bool register_fs_ops(dev_t dev, std::shared_ptr<vfs::ops> ops)
    {
        auto [it, inserted] = fs_ops_table.write_lock()->emplace(dev, ops);
        if (inserted)
            lib::debug("dev: registered ops for filesystem with id {}", dev);
        return inserted;
    }

    auto get_ops(dev_t dev, dev_t rdev, mode_t mode) -> lib::expect<std::shared_ptr<vfs::ops>>
    {
        switch (stat::type(mode))
        {
            case stat::type::s_ifchr:
            case stat::type::s_ifblk:
            {
                if (rdev == 0)
                    return std::unexpected { lib::err::invalid_device_or_address };
                auto ops = get_dev_ops(rdev);
                if (!ops)
                    return std::unexpected { lib::err::invalid_device_or_address };
                return ops;
            }
            case stat::type::s_ififo:
                return pipe::get_ops();
            case stat::type::s_ifsock:
                return std::unexpected { lib::err::todo };
            case stat::type::s_ifreg:
            {
                auto ops = get_fs_ops(dev);
                if (!ops)
                    return std::unexpected { lib::err::invalid_device_or_address };
                return ops;
            }
            // case stat::type::s_ifdir:
            // case stat::type::s_iflnk:
            default:
                return std::unexpected { lib::err::invalid_type };
        }
        std::unreachable();
    }
} // namespace vfs::dev