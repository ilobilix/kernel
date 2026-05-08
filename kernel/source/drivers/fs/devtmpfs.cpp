// Copyright (C) 2024-2026  ilobilo

module drivers.fs.devtmpfs;

import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;

namespace fs::devtmpfs
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

        fs() : vfs::filesystem { "devtmpfs" }
        {
            instance = lib::make_locked<tmpfs::fs::instance, sched::mutex>();
            auto locked = instance.lock();

            locked->opt_mode = 0755;

            root = std::make_shared<vfs::dentry>();
            root->name = "devtmpfs root. this shouldn't be visible anywhere";
            root->inode = std::make_shared<tmpfs::inode>(
                locked.get(), locked->dev_id, 0, locked->next_inode++,
                static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode
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

    lib::expect<void> create(lib::path path, mode_t mode, dev_t rdev)
    {
        if (main == nullptr)
            return std::unexpected { lib::err::invalid_filesystem };

        if (path.empty() || path == "." || path.is_absolute() || path.str().starts_with("dev/"))
            return std::unexpected { lib::err::invalid_path };

        const vfs::path devroot {
            .mnt = main->internal_mnt,
            .dentry = main->root
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
            lib::bug_on(!vfs::register_fs(std::unique_ptr<vfs::filesystem> { main = new fs }));
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
