// Copyright (C) 2024-2026  ilobilo

module drivers.fs.securityfs;

import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;
import system.vfs;
import lib;
import std;

// TODO

namespace fs::securityfs
{
    struct fs : vfs::filesystem
    {
        lib::locked_ptr<tmpfs::fs::instance, sched::mutex> instance;
        std::shared_ptr<vfs::dentry> root;

        mutable lib::list<std::shared_ptr<struct vfs::mount>> mounts;

        auto mount(
            std::shared_ptr<vfs::dentry> src,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const
            -> lib::expect<std::shared_ptr<struct vfs::mount>> override
        {
            lib::unused(src, data);

            auto mount = std::make_shared<struct vfs::mount>(instance, root, std::nullopt);
            mounts.push_back(mount);
            return mount;
        }

        fs() : vfs::filesystem { "securityfs" }
        {
            instance = lib::make_locked<tmpfs::fs::instance, sched::mutex>();
            auto locked = instance.lock();

            locked->opt_mode = 0755;

            root = std::make_shared<vfs::dentry>();
            root->name = "securityfs root. this shouldn't be visible anywhere";
            root->inode = std::make_shared<tmpfs::inode>(
                locked.get(), locked->dev_id, 0, locked->next_inode++,
                static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode
            );
            root->parent = root;

            vfs::dev::register_fs_ops(locked->dev_id, tmpfs::ops::singleton());
        }
    };

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.securityfs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.securityfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { registered_stage() },
        [] {
            lib::bug_on(!vfs::register_fs(std::unique_ptr<vfs::filesystem> { new fs }));
        }
    };
} // namespace fs::securityfs
