// Copyright (C) 2024-2025  ilobilo

module system.vfs;

import system.scheduler;
import system.cpu.self;
import drivers.fs;
import lib;
import cppstd;

namespace vfs
{
    namespace
    {
        std::shared_ptr<dentry> root = [] {
            auto root = std::make_shared<dentry>();
            root->name = "/";
            root->parent = root;
            root->inode = std::make_shared<inode>(nullptr);
            root->inode->stat.st_mode = stat::s_ifdir;
            return root;
        } ();
        lib::map::flat_hash<std::string_view, std::unique_ptr<filesystem>> filesystems;
        lib::mutex lock;
    } // namespace

    bool register_fs(std::unique_ptr<filesystem> fs)
    {
        const std::unique_lock _ { lock };
        if (filesystems.contains(fs->name))
            return false;

        log::info("vfs: registering filesystem '{}'", fs->name);
        filesystems[fs->name] = std::move(fs);
        return true;
    }

    auto find_fs(std::string_view name) -> expect<std::reference_wrapper<std::unique_ptr<filesystem>>>
    {
        const std::unique_lock _ { lock };
        if (auto it = filesystems.find(name); it != filesystems.end())
            return it->second;
        return std::unexpected(error::invalid_filesystem);
    }

    std::shared_ptr<dentry> dentry::root(bool from_sched)
    {
        if (!from_sched && sched::is_initialised())
            return sched::proc_for(sched::percpu->running_thread->pid)->root;
        return vfs::root;
    }

    auto dentry::reduce(std::size_t symlink_depth) -> expect<std::shared_ptr<dentry>>
    {
        auto current = shared_from_this();

        // TODO
        lib::unused(symlink_depth);

        // if (symlinked_to.empty())
        //     return current;

        // auto og = symlink_depth;
        // while (symlink_depth > 0)
        // {
        //     auto parent = current->parent.lock();
        //     auto ret = resolve(nullptr, parent, current->symlinked_to);
        //     if (!ret || ret->target == current)
        //         return std::unexpected(error::invalid_symlink);

        //     current = ret->target;
        //     symlink_depth--;
        // }

        // if (og && symlink_depth == 0 && !current->symlinked_to.empty())
        //     return std::unexpected(error::symloop_max);

        return current;
    }

    auto path_for(lib::path _path) -> expect<path>
    {
        auto res = resolve(std::nullopt, _path);
        // TODO: relative path based on cwd
        return res->target;
    }

    auto resolve(std::optional<path> parent, lib::path _path) -> expect<resolve_res>
    {
        auto root = dentry::root();

        if (!parent)
        {
            parent = path { .mnt = nullptr, .dentry = root };
            if (!root->child_mounts.empty())
                parent->mnt = root->child_mounts.front().lock();
        }
        lib::ensure(parent.has_value());

        if (_path == "/" || _path.empty() || _path == ".")
            return resolve_res { parent.value(), parent.value() };

        lib::ensure(parent->mnt != nullptr);

        auto current = parent.value();

        auto split = std::views::split(_path.str(), '/');
        const std::size_t size = std::ranges::distance(split);
        for (std::size_t i = 0; const auto segment_view : split)
        {
            i++;
            const std::string_view segment { segment_view };
            if (segment.empty())
                continue;

            const bool last = i == size;

            if (segment == "..")
            {
                auto resolve_mounts = [](auto path)
                {
                    while (path.dentry == path.mnt->root)
                        path = path.mnt->mounted_on.value();
                    return path;
                };

                current = resolve_mounts(current);
                current.dentry = current.dentry->parent.lock();
                current = resolve_mounts(current);

                if (last)
                {
                    auto parent = resolve_mounts(current);
                    parent.dentry = parent.dentry->parent.lock();
                    parent = resolve_mounts(parent);
                    return resolve_res { parent, current };
                }
                continue;
            }

            try_again:
            const auto &children = current.dentry->children;
            if (auto it = children.find(segment); it != children.end())
            {
                auto dentry = it->second;
                auto mnt = current.mnt;

                again:
                for (const auto &child_mnt : dentry->child_mounts)
                {
                    const auto locked = child_mnt.lock();
                    if (mnt == locked->mounted_on->mnt)
                    {
                        mnt = locked;
                        dentry = locked->root;
                        goto again;
                    }
                }

                path next { mnt, dentry };

                if (last)
                    return resolve_res { current, next };

                current = next;
                if (current.dentry->inode->stat.type() != stat::type::s_ifdir)
                    return std::unexpected(error::not_a_dir);

                continue;
            }

            if (populate(current, segment))
                goto try_again;

            break;
        }
        return std::unexpected(error::not_found);
    }

    auto mount(lib::path source_path, lib::path target_path, std::string_view fstype, int flags) -> expect<void>
    {
        // TODO
        lib::unused(flags);

        auto fs = find_fs(fstype);
        if (!fs)
            return std::unexpected(fs.error());

        const std::unique_lock _ { lock };

        std::optional<path> source { };
        if (!source_path.empty())
        {
            auto ret = path_for(source_path);
            if (!ret)
                return std::unexpected(ret.error());

            source = ret.value();
            if (source->dentry->inode->stat.type() != stat::type::s_ifblk)
                return std::unexpected(error::not_a_block);
        }

        auto ret = path_for(target_path);
        if (!ret)
            return std::unexpected(ret.error());

        auto target = ret.value();
        if (target.dentry->inode->stat.type() != stat::type::s_ifdir)
            return std::unexpected(error::not_a_dir);

        auto mnt = fs->get()->mount(source ? std::optional { source->dentry } : std::nullopt);
        if (!mnt)
            return std::unexpected(mnt.error());

        mnt.value()->mounted_on = target;
        target.dentry->child_mounts.push_back(mnt.value());

        log::info("vfs: mount('{}', '{}', '{}')", source_path, target_path, fstype);

        return { };
    }

    auto unmount(lib::path target) -> expect<void>
    {
        lib::unused(target);
        return std::unexpected(error::todo);
    }

    auto create(std::optional<path> parent, lib::path _path, mode_t mode) -> expect<path>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(parent, _path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, _path.dirname());
        if (!res)
            return std::unexpected(res.error());

        const auto real_parent = res->target;
        const auto name = _path.basename();

        auto ret = real_parent.mnt->fs->create(real_parent.dentry->inode, name, mode);
        if (ret)
        {
            auto dentry = std::make_shared<vfs::dentry>();
            dentry->parent = real_parent.dentry;
            dentry->name = name;
            dentry->inode = ret.value();

            const std::unique_lock _ { real_parent.dentry->lock };
            real_parent.dentry->children[dentry->name] = dentry;
            return path { real_parent.mnt, dentry };
        }
        return std::unexpected(ret.error());
    }

    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> expect<path>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(parent, src);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, src.dirname());
        if (!res)
            return std::unexpected(res.error());

        const auto real_parent = res->target;
        const auto name = src.basename();

        auto ret = real_parent.mnt->fs->symlink(real_parent.dentry->inode, name, target);
        if (ret)
        {
            auto dentry = std::make_shared<vfs::dentry>();
            dentry->parent = real_parent.dentry;
            dentry->name = name;
            dentry->inode = ret.value();

            const std::unique_lock _ { real_parent.dentry->lock };
            real_parent.dentry->children[dentry->name] = dentry;
            return path { real_parent.mnt, dentry };
        }
        return std::unexpected(ret.error());
    }

    auto link(std::optional<path> parent, lib::path src, std::optional<path> tgtparent, lib::path target) -> expect<path>
    {
        lib::unused(parent, src, tgtparent, target);
        return std::unexpected(error::todo);
    }

    auto unlink(std::optional<path> parent, lib::path path) -> expect<void>
    {
        lib::unused(parent, path);
        return std::unexpected(error::todo);
    }

    auto stat(std::optional<path> parent, lib::path path) -> expect<::stat>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(parent, path);
        if (!res)
            return std::unexpected(res.error());

        return res->target.dentry->inode->stat;
    }

    bool populate(path parent, std::string_view name)
    {
        const auto ret = parent.mnt->fs->populate(parent.dentry->inode, name);
        if (!ret.has_value())
            return false;

        const auto list = ret.value();
        if (!list.empty())
        {
            for (const auto [name, inode] : list)
            {
                auto dentry = std::make_shared<vfs::dentry>();
                dentry->parent = parent.dentry;
                dentry->name = name;
                dentry->inode = inode;

                const std::unique_lock _ { parent.dentry->lock };
                parent.dentry->children[dentry->name] = dentry;
            }
            return true;
        }
        return false;
    }

    initgraph::stage *root_mounted_stage()
    {
        static initgraph::stage stage { "vfs.root-mounted" };
        return &stage;
    }

    initgraph::task fs_task
    {
        "vfs.mount-root",
        initgraph::require { fs::filesystems_registered_stage() },
        initgraph::entail { root_mounted_stage() },
        [] {
            lib::ensure(mount("", "/", "tmpfs", 0));

            auto err = create(std::nullopt, "/dev", stat::type::s_ifdir);
            lib::ensure(err.has_value() || err.error() == error::already_exists);
            lib::ensure(mount("", "/dev", "devtmpfs", 0));
        }
    };
} // namespace vfs