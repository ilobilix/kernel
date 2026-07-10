// Copyright (C) 2024-2026  ilobilo

module drivers.fs.devpts;

import drivers.fs.devtmpfs;
import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;
import frigg;
import fmt;

namespace fs::devpts
{
    namespace
    {
        struct fs_t : vfs::filesystem_t
        {
            lib::locked_ptr<tmpfs::fs_t::instance, sched::mutex> instance;
            std::shared_ptr<vfs::dentry_t> root;

            std::shared_ptr<struct vfs::mount_t> internal_mnt;
            mutable lib::list<std::shared_ptr<struct vfs::mount_t>> mounts;

            auto mount(
                std::shared_ptr<vfs::dentry_t> src,
                std::optional<lib::maybe_uspan<const std::byte>> data
            ) const
                -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
            {
                lib::unused(src, data);

                auto mount = std::make_shared<struct vfs::mount_t>(instance, root);
                mounts.push_back(mount);
                return mount;
            }

            fs_t() : vfs::filesystem_t { "devpts", 0x1CD1 }
            {
                instance = lib::make_locked<tmpfs::fs_t::instance, sched::mutex>();
                auto locked = instance.lock();

                locked->fs = this;
                locked->opt_mode = 0755;

                root = std::make_shared<vfs::dentry_t>();
                root->name = "devpts root. this shouldn't be visible anywhere";
                root->inode = std::make_shared<tmpfs::inode_t>(
                    locked.get(), locked->dev_id, 0, locked->next_inode++,
                    static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode,
                    tmpfs::ops::singleton()
                );
                root->parent = root;

                internal_mnt = std::make_shared<struct vfs::mount_t>(instance, root);
            }
        };

        frg::manual_box<fs_t> fs;
    } // namespace

    lib::expect<vfs::path_t> attach_slave(std::uint32_t minor, mode_t mode, dev_t rdev)
    {
        if (!fs.valid())
            return std::unexpected { lib::err::invalid_filesystem };

        return vfs::create(vfs::path_t {
            .mnt = fs->internal_mnt,
            .dentry = fs->root
        }, fmt::format("{}", minor), mode, rdev);
    }

    lib::expect<void> detach_slave(std::uint32_t minor)
    {
        if (!fs.valid())
            return std::unexpected { lib::err::invalid_filesystem };

        return vfs::unlink(vfs::path_t {
            .mnt = fs->internal_mnt,
            .dentry = fs->root
        }, fmt::format("{}", minor));
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
            fs.initialize();
            lib::bug_on(!vfs::register_fs(*fs));
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
