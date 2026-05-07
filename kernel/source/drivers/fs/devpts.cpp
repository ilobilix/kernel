// Copyright (C) 2024-2026  ilobilo

module drivers.fs.devpts;

import drivers.fs.devtmpfs;
import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;
import system.vfs;
import lib;
import std;
import fmt;

namespace fs::devpts
{
    struct fs : vfs::filesystem
    {
        lib::locked_ptr<tmpfs::fs::instance, sched::mutex> instance;
        std::shared_ptr<vfs::dentry> root;

        std::shared_ptr<struct vfs::mount> internal_mnt;
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

        fs() : vfs::filesystem { "devpts" }
        {
            instance = lib::make_locked<tmpfs::fs::instance, sched::mutex>();
            auto locked = instance.lock();

            root = std::make_shared<vfs::dentry>();
            root->name = "devpts root. this shouldn't be visible anywhere";
            root->inode = std::make_shared<tmpfs::inode>(
                locked.get(), locked->dev_id, 0, locked->next_inode++,
                static_cast<mode_t>(stat::type::s_ifdir) | 0755
            );
            root->parent = root;

            internal_mnt = std::make_shared<struct vfs::mount>(instance, root, std::nullopt);

            vfs::dev::register_fs_ops(locked->dev_id, tmpfs::ops::singleton());
        }
    };

    namespace
    {
        fs *main = nullptr;
    } // namespace

    lib::expect<vfs::path> attach_slave(std::uint32_t minor, mode_t mode, dev_t rdev)
    {
        if (main == nullptr)
            return std::unexpected { lib::err::invalid_filesystem };

        return vfs::create(vfs::path {
            .mnt = main->internal_mnt,
            .dentry = main->root
        }, fmt::format("{}", minor), mode, rdev);
    }

    lib::expect<void> detach_slave(std::uint32_t minor)
    {
        if (main == nullptr)
            return std::unexpected { lib::err::invalid_filesystem };

        return vfs::unlink(vfs::path {
            .mnt = main->internal_mnt,
            .dentry = main->root
        }, fmt::format("{}", minor));
    }

    std::unique_ptr<vfs::filesystem> init()
    {
        if (main != nullptr)
            lib::panic("devpts: tried to initialise twice");

        return std::unique_ptr<vfs::filesystem> { main = new fs };
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.devpts.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.devpts.mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.devpts.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { registered_stage() },
        [] {
            lib::bug_on(!vfs::register_fs(devpts::init()));
        }
    };

    lib::initgraph::task mount_task
    {
        "vfs.devpts.mount",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            vfs::root_mounted_stage(),
            devtmpfs::mounted_stage(),
            registered_stage()
        },
        lib::initgraph::entail { mounted_stage() },
        [] {
            const auto cerr = vfs::create(std::nullopt, "/dev/pts", stat::type::s_ifdir | 0755);
            if (!cerr && cerr.error() != lib::err::already_exists)
            {
                lib::panic(
                    "devpts: failed to create directory '/dev/pts': {}",
                    lib::error_name(cerr.error())
                );
            }

            if (const auto merr = vfs::mount("", "/dev/pts", "devpts", 0); !merr)
            {
                lib::panic(
                    "devpts: failed to mount devpts at '/dev/pts': {}",
                    lib::error_name(merr.error())
                );
            }
        }
    };
} // namespace fs::devpts
