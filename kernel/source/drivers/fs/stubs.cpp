// Copyright (C) 2024-2026  ilobilo

module drivers.fs.stubs;

import drivers.fs.tmpfs;
import system.sched.mutex;
import system.vfs.dev;
import frigg;
import fmt;

namespace fs::stubs
{
    namespace
    {
        struct stub_fs : vfs::filesystem_t
        {
            lib::locked_ptr<tmpfs::fs_t::instance, sched::mutex> instance;
            std::shared_ptr<vfs::dentry_t> root;
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

            stub_fs(std::string_view name, std::uint32_t magic)
                : vfs::filesystem_t { name, magic }
            {
                instance = lib::make_locked<tmpfs::fs_t::instance, sched::mutex>();
                auto locked = instance.lock();

                locked->fs = this;
                locked->opt_mode = 0755;

                root = std::make_shared<vfs::dentry_t>();
                root->name = fmt::format("{} stub root. this shouldn't be visible anywhere", name);
                root->inode = std::make_shared<tmpfs::inode_t>(
                    locked.get(), locked->dev_id, 0, locked->next_inode++,
                    static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode,
                    tmpfs::ops::singleton()
                );
                root->parent = root;
            }
        };

        struct entry
        {
            std::string_view name;
            std::uint32_t magic;
            frg::manual_box<stub_fs> fs;
        };

        constinit std::array<entry, 5> stubs {{
            { "bpf"sv, 0xCAFE4A11, { } },
            { "cgroup2"sv, 0x63677270, { } },
            { "pstore"sv, 0x6165676C, { } },
            { "securityfs"sv, 0x73636673, { } }
        }};
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
            for (auto &[name, magic, fs] : stubs)
            {
                fs.initialize(name, magic);
                lib::bug_on(!vfs::register_fs(*fs));
            }
        }
    };
} // namespace fs::stubs
