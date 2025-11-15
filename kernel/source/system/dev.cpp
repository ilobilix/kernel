// Copyright (C) 2024-2025  ilobilo

module system.dev;

import drivers.initramfs;
import system.vfs;
import magic_enum;
import lib;
import cppstd;

namespace dev
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
            log::debug("dev: registed characted device ({}, {})", major(dev), minor(dev));
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

    lib::initgraph::stage *populated_stage()
    {
        static lib::initgraph::stage stage
        {
            "dev.populated",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task populate_task
    {
        "dev.populate",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { initramfs::extracted_stage() },
        lib::initgraph::entail { populated_stage() },
        [] {
            auto create = [](std::string_view path, mode_t mode, dev_t dev)
            {
                auto ret = vfs::create(std::nullopt, path, mode, dev);
                lib::panic_if(
                    !ret && ret.error() != vfs::error::already_exists,
                    "dev: failed to create a character device file '{}': {}", path, magic_enum::enum_name(ret.error())
                );
            };

            create("/dev/null", stat::s_ifchr | 0666, dev::makedev(1, 3));
            create("/dev/zero", stat::s_ifchr | 0666, dev::makedev(1, 5));
            create("/dev/full", stat::s_ifchr | 0666, dev::makedev(1, 7));
            create("/dev/random", stat::s_ifchr | 0666, dev::makedev(1, 8));
            create("/dev/urandom", stat::s_ifchr | 0666, dev::makedev(1, 9));
        }
    };
} // namespace dev