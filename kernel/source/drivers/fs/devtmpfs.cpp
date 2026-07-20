// Copyright (C) 2024-2026  ilobilo

module drivers.fs.devtmpfs;

import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;
import frigg;

namespace fs::devtmpfs
{
    namespace
    {
        struct fs_t : vfs::filesystem_t
        {
            lib::locked_ptr<tmpfs::fs_t::instance, sched::mutex_t> instance;
            std::shared_ptr<vfs::dentry_t> root;

            std::shared_ptr<struct vfs::mount_t> internal_mnt;

            auto mount(
                std::shared_ptr<vfs::dentry_t> src, std::uint64_t flags,
                std::optional<lib::maybe_uspan<const std::byte>> data
            ) const
                -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
            {
                lib::unused(src, data, flags);
                return std::make_shared<struct vfs::mount_t>(instance, root);
            }

            fs_t() : vfs::filesystem_t { "devtmpfs", 0x01021994 }
            {
                instance = lib::make_locked<tmpfs::fs_t::instance, sched::mutex_t>();
                auto locked = instance.lock();

                locked->fs = this;
                locked->opt_mode = 0755;

                root = std::make_shared<vfs::dentry_t>();
                root->name = "devtmpfs root. this shouldn't be visible anywhere";
                root->inode = std::make_shared<tmpfs::inode_t>(
                    locked.get(), locked->dev_id, 0, locked->next_inode++,
                    static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode,
                    tmpfs::ops_t::singleton()
                );
                root->parent = root;

                internal_mnt = std::make_shared<struct vfs::mount_t>(instance, root);
            }
        };

        frg::manual_box<fs_t> fs;
    } // namespace

    lib::expect<void> create(lib::path path, mode_t mode, dev_t rdev)
    {
        if (!fs.valid())
            return std::unexpected { lib::err::invalid_filesystem };

        if (path.empty() || path == "." || path.is_absolute() || path.str().starts_with("dev/"))
            return std::unexpected { lib::err::invalid_path };

        const vfs::path_t devroot {
            .mnt = fs->internal_mnt,
            .dentry = fs->root
        };

        std::string partial;
        for (const auto segment_view : path.dirname() | std::views::split('/'))
        {
            const std::string_view segment { segment_view };
            if (segment.empty())
                continue;

            if (!partial.empty())
                partial += '/';
            partial += segment;

            if (const auto ret = vfs::create(devroot, partial, stat::type::s_ifdir | 0755); !ret)
            {
                if (ret.error() != lib::err::already_exists)
                    return std::unexpected { ret.error() };
            }
        }

        if (const auto ret = vfs::create(devroot, path, mode, rdev); !ret)
            return std::unexpected { ret.error() };

        return { };
    }

    lib::expect<void> remove(lib::path path)
    {
        if (!fs.valid())
            return std::unexpected { lib::err::invalid_filesystem };

        if (path.empty() || path == "." || path.is_absolute() || path.str().starts_with("dev/"))
            return std::unexpected { lib::err::invalid_path };

        const vfs::path_t devroot {
            .mnt = fs->internal_mnt,
            .dentry = fs->root
        };

        if (const auto ret = vfs::unlink(devroot, path); !ret)
            return std::unexpected { ret.error() };

        return { };
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
            fs.initialize();
            lib::bug_on(!vfs::register_fs(*fs));
        }
    };

    lib::initgraph::task mount_task
    {
        "vfs.devtmpfs.mount",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            vfs::root_mounted_stage(),
            registered_stage()
        },
        lib::initgraph::entail { mounted_stage() },
        [] {
            const auto cerr = vfs::create(std::nullopt, "/dev", stat::type::s_ifdir | 0755);
            if (!cerr && cerr.error() != lib::err::already_exists)
            {
                lib::panic(
                    "devtmpfs: failed to create directory '/dev': {}",
                    lib::error_name(cerr.error())
                );
            }
            if (const auto merr = vfs::mount("", "/dev", "devtmpfs", 0); !merr)
            {
                lib::panic(
                    "devtmpfs: failed to mount devtmpfs at '/dev': {}",
                    lib::error_name(merr.error())
                );
            }
        }
    };
} // namespace fs::devtmpfs
