// Copyright (C) 2024-2026  ilobilo

module drivers.fs.cgroupfs;

import drivers.fs.tmpfs;
import system.sched.mutex;
import system.dev;
import frigg;
import fmt;

// TODO: this is just a stub

namespace fs::cgroupfs
{
    namespace
    {
        constexpr std::array v1_files {
            "cgroup.clone_children"sv, "cgroup.procs"sv,
            "notify_on_release"sv, "release_agent"sv, "tasks"sv
        };

        constexpr std::array v2_files {
            "cgroup.controllers"sv, "cgroup.events"sv, "cgroup.max.depth"sv,
            "cgroup.max.descendants"sv, "cgroup.procs"sv, "cgroup.stat"sv,
            "cgroup.subtree_control"sv, "cgroup.threads"sv, "cgroup.type"sv
        };

        constexpr std::array ignored_opts {
            "none"sv, "nsdelegate"sv, "memory_recursiveprot"sv, "noprefix"sv,
            "xattr"sv, "clone_children"sv, "release_agent"sv, "ro"sv, "rw"sv,
            "nosuid"sv, "nodev"sv, "noexec"sv, "relatime"sv, "noatime"sv
        };

        struct hierarchy_t
        {
            std::size_t id;
            std::string controllers;
        };
        lib::locker<
            std::vector<hierarchy_t>,
            sched::mutex_t
        > hierarchies;

        std::size_t register_hierarchy(std::string controllers)
        {
            auto locked = hierarchies.lock();
            for (const auto &entry : *locked)
            {
                if (entry.controllers == std::string_view { controllers })
                    return entry.id;
            }

            const auto id = locked->size() + 1;
            locked->push_back({ id, std::move(controllers) });
            return id;
        }

        std::string read_options(std::optional<lib::maybe_uspan<const std::byte>> data)
        {
            if (!data)
                return { };

            const auto size = std::min(data->size(), 4096uz);

            std::string str;
            str.resize(size);
            if (!data->subspan(0, size).copy_to(reinterpret_cast<std::byte *>(str.data())))
                return { };

            if (const auto end = str.find('\0'); end != std::string::npos)
                str.resize(end);
            return str;
        }

        std::string controllers_of(std::string_view opts)
        {
            std::string out;
            while (!opts.empty())
            {
                const auto comma = opts.find(',');
                const auto opt = opts.substr(0, comma);
                opts = comma == std::string_view::npos
                    ? std::string_view { }
                    : opts.substr(comma + 1);

                if (opt.empty() || (opt.contains('=') && !opt.starts_with("name=")))
                    continue;

                if (std::ranges::find(ignored_opts, opt) != ignored_opts.end())
                    continue;

                if (!out.empty())
                    out.append(",");
                out.append(opt);
            }
            return out;
        }

        struct instance_t : tmpfs::fs_t::instance
        {
            std::span<const std::string_view> files;

            void populate(const std::shared_ptr<vfs::dentry_t> &dir)
            {
                if (!dir->inode || dir->inode->stat.type() != stat::type::s_ifdir)
                    return;

                const auto locked = dir->children.lock();
                for (const auto name : files)
                {
                    if (locked->lookup(name))
                        continue;

                    auto inode = create(
                        dir->inode, name,
                        static_cast<mode_t>(stat::type::s_ifreg) | 0644,
                        0, std::nullopt
                    );
                    if (!inode)
                        continue;

                    auto child = vfs::dentry_t::create();
                    child->name = name;
                    child->inode = std::move(*inode);
                    child->parent = dir;
                    locked->insert(std::move(child));
                }
            }

            auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override
            {
                populate(dir);
                return tmpfs::fs_t::instance::readdir(std::move(dir), cookie);
            }

            auto lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
                -> lib::expect<vfs::dir_entry> override
            {
                populate(dir);
                return tmpfs::fs_t::instance::lookup(std::move(dir), name);
            }

            ~instance_t() = default;
        };

        struct fs_t : vfs::filesystem_t
        {
            const bool unified;

            auto mount(
                std::shared_ptr<vfs::dentry_t> src, std::uint64_t flags,
                std::optional<lib::maybe_uspan<const std::byte>> data
            ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
            {
                lib::unused(src, flags);

                auto controllers = controllers_of(read_options(data));
                if (!unified && controllers.empty())
                    return std::unexpected { lib::err::invalid_argument };

                auto instance = lib::make_locked<cgroupfs::instance_t, sched::mutex_t>();
                auto locked = instance.lock();

                locked->fs = const_cast<fs_t *>(this);
                locked->opt_mode = 0755;
                locked->files = unified
                    ? std::span<const std::string_view> { v2_files }
                    : std::span<const std::string_view> { v1_files };

                auto root = std::make_shared<vfs::dentry_t>();
                root->name = fmt::format("{} root. this shouldn't be visible anywhere", name);
                root->inode = std::make_shared<tmpfs::inode_t>(
                    locked.get(), locked->dev_id, 0, locked->next_inode++,
                    static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode,
                    tmpfs::ops_t::singleton()
                );
                root->parent = root;

                if (!unified)
                    register_hierarchy(std::move(controllers));
                return std::make_shared<struct vfs::mount_t>(std::move(instance), root);
            }

            fs_t(std::string_view name, std::uint32_t magic, bool unified)
                : vfs::filesystem_t { name, magic }, unified { unified } { }
        };

        frg::manual_box<fs_t> legacy_fs;
        frg::manual_box<fs_t> unified_fs;

        std::shared_ptr<dev::kobject_t> cgroup_kobj;
    } // namespace

    std::string proc_lines()
    {
        std::string out;
        {
            const auto locked = hierarchies.lock();
            for (const auto &entry : locked.value())
                out.append(fmt::format("{}:{}:/\n", entry.id, entry.controllers));
        }

        out.append("0::/\n");
        return out;
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.cgroupfs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.cgroupfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { dev::core_registered_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            legacy_fs.initialize("cgroup"sv, 0x27E0EB, false);
            unified_fs.initialize("cgroup2"sv, 0x63677270, true);

            lib::bug_on(!vfs::register_fs(*legacy_fs));
            lib::bug_on(!vfs::register_fs(*unified_fs));

            cgroup_kobj = dev::kobject_t::create(
                "cgroup", dev::empty_ktype(), dev::root("/fs")
            );
            lib::bug_on(!dev::register_kobject(cgroup_kobj));
        }
    };
} // namespace fs::cgroupfs
