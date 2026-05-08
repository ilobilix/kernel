// Copyright (C) 2024-2026  ilobilo

module drivers.fs.stubs;

import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;
import fmt;

namespace fs::stubs
{
    namespace
    {
        struct entry
        {
            std::string_view name;
            std::uint32_t magic;
        };

        constexpr std::array<entry, 5> stubs {{
            { "bpf"sv, 0xCAFE4A11 },
            { "cgroup2"sv, 0x63677270 },
            { "pstore"sv, 0x6165676C },
            { "securityfs"sv, 0x73636673 },
            { "sysfs"sv, 0x62656572 }
        }};

        struct stub_fs : vfs::filesystem
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

            stub_fs(std::string name, std::uint32_t magic)
                : vfs::filesystem { std::move(name), magic }
            {
                instance = lib::make_locked<tmpfs::fs::instance, sched::mutex>();
                auto locked = instance.lock();

                locked->fs = this;
                locked->opt_mode = 0755;

                root = std::make_shared<vfs::dentry>();
                root->name = fmt::format("{} stub root. this shouldn't be visible anywhere", this->name);
                root->inode = std::make_shared<tmpfs::inode>(
                    locked.get(), locked->dev_id, 0, locked->next_inode++,
                    static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode
                );
                root->parent = root;

                vfs::dev::register_fs_ops(locked->dev_id, tmpfs::ops::singleton());
            }
        };
    } // namespace

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.stubs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.stubs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { registered_stage() },
        [] {
            for (const auto &[name, magic] : stubs)
                lib::bug_on(!vfs::register_fs(std::make_unique<stub_fs>(std::string { name }, magic)));
        }
    };
} // namespace fs::stubs
