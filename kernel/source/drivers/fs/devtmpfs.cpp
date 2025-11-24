// Copyright (C) 2024-2025  ilobilo

module drivers.fs.devtmpfs;

import drivers.fs.tmpfs;
import system.vfs;
import magic_enum;
import lib;
import cppstd;

namespace fs::devtmpfs
{
    struct fs : vfs::filesystem
    {
        lib::locked_ptr<tmpfs::fs::instance, lib::mutex> instance;
        std::shared_ptr<vfs::dentry> root;
        mutable std::list<std::shared_ptr<struct vfs::mount>> mounts;

        auto mount(std::shared_ptr<vfs::dentry> src) const -> vfs::expect<std::shared_ptr<struct vfs::mount>> override
        {
            lib::unused(src);

            auto mount = std::make_shared<struct vfs::mount>(instance, root, std::nullopt);
            mounts.push_back(mount);
            return mount;
        }

        fs() : vfs::filesystem { "devtmpfs" }
        {
            instance = lib::make_locked<tmpfs::fs::instance, lib::mutex>();
            auto locked = instance.lock();

            root = std::make_shared<vfs::dentry>();
            root->name = "devtmpfs root. this shouldn't be visible anywhere";
            root->inode = std::make_shared<tmpfs::inode>(
                locked->dev_id, 0, locked->next_inode++,
                static_cast<mode_t>(stat::type::s_ifdir),
                tmpfs::ops::singleton()
            );
        }
    };

    std::unique_ptr<vfs::filesystem> init()
    {
        static bool once_flag = false;
        if (once_flag)
            lib::panic("devtmpfs: tried to initialise twice");

        once_flag = true;
        return std::unique_ptr<vfs::filesystem> { new fs };
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.devtmpfs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.devtmpfs.mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.devtmpfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { registered_stage() },
        [] {
            lib::bug_on(!vfs::register_fs(devtmpfs::init()));
        }
    };

    lib::initgraph::task mount_task
    {
        "vfs.devtmpfs.mount",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { vfs::root_mounted_stage(), registered_stage() },
        lib::initgraph::entail { mounted_stage() },
        [] {
            const auto cerr = vfs::create(std::nullopt, "/dev", stat::type::s_ifdir);
            lib::panic_if(
                !cerr && cerr.error() != vfs::error::already_exists,
                "devtmpfs: failed to create directory '/dev': {}", magic_enum::enum_name(cerr.error())
            );
            const auto merr = vfs::mount("", "/dev", "devtmpfs", 0);
            lib::panic_if(!merr, "devtmpfs: failed to mount devtmpfs at '/dev': {}", magic_enum::enum_name(merr.error()));
        }
    };
} // namespace fs::devtmpfs