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
        std::shared_ptr<node> root = [] {
            auto root = std::make_shared<node>();
            root->name = "/";
            root->parent = root;
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

    std::shared_ptr<node> node::root(bool from_sched)
    {
        if (!from_sched && sched::initialised)
            return sched::proc_for(sched::percpu->running_thread->pid)->root;
        return vfs::root->reduce().value();
    }

    auto node::reduce(std::size_t symlink_depth) -> expect<std::shared_ptr<node>>
    {
        auto current = shared_from_this();
        if (mountpoint)
        {
            auto ret = mountpoint->reduce(symlink_depth);
            if (!ret)
                return std::unexpected(error::invalid_mount);

            if (ret.value() != mountpoint)
                current = ret.value();
        }

        if (symlinked_to.empty())
            return current;

        auto og = symlink_depth;
        while (symlink_depth > 0)
        {
            auto parent = current->parent.lock();
            auto ret = resolve(nullptr, parent, current->symlinked_to);
            if (!ret || ret->target == current)
                return std::unexpected(error::invalid_symlink);

            current = ret->target;
            symlink_depth--;
        }

        if (og && symlink_depth == 0 && !current->symlinked_to.empty())
            return std::unexpected(error::symloop_max);

        return current;
    }

    auto mount_for(std::shared_ptr<node> parent, lib::path path) -> expect<std::shared_ptr<struct mount>>
    {
        auto res = resolve(nullptr, parent, path);
        auto node = res->target->me();
        if (!node->inode)
            return std::unexpected(error::invalid_mount);
        return node->inode->mount;
    }

    auto resolve(std::shared_ptr<struct mount> mount, std::shared_ptr<node> parent, lib::path path) -> expect<resolve_res>
    {
        auto root = node::root()->me();

        if (parent)
        {
            auto ret = parent->reduce();
            if (!ret)
                return std::unexpected(error::not_found);
            parent = ret.value();
        }
        else parent = root;

        if (mount == nullptr && parent->inode)
            mount = parent->inode->mount;

        if (path == "/" || path.empty() || path == ".")
            return resolve_res { parent, parent };

        auto current = parent;

        bool first_mount = true;

        auto split = std::views::split(path.str(), '/');
        const std::size_t size = std::ranges::distance(split);
        for (std::size_t i = 0; const auto segment_view : split)
        {
            i++;
            const std::string_view segment { segment_view };
            if (segment.empty())
                continue;

            const bool last = i == size;
            auto current_me = current->me();

            if (segment == "..")
            {
                auto get_parent = [&first_mount, &current, &current_me, &mount]
                {
                    auto parent = current->parent.lock();
                    if (auto root = node::root(); current == root)
                        parent = root;
                    else if (current_me == current->inode->mount->root.lock())
                    {
                        parent = (first_mount ? mount : current->inode->mount)->mounted_on.lock()->parent.lock();
                        first_mount = false;
                    }
                    return parent;
                };

                auto parent = get_parent();
                if (!parent)
                    return std::unexpected(error::not_found);

                current = parent;
                current_me = current->me();

                if (current_me == current->inode->mount->root.lock())
                {
                    current = (first_mount ? mount : current->inode->mount)->mounted_on.lock()   ;
                    current_me = current->me();
                    first_mount = false;
                }

                if (last)
                    return resolve_res { get_parent(), current };

                continue;
            }

            lib::ensure(current_me->inode->mount.get() != nullptr);

            try_again:
            auto &children = current_me->get_children();
            if (auto it = children.find(segment); it != children.end())
            {
                auto node = it->second->reduce(0);
                if (!node)
                    return std::unexpected(node.error());

                if (last)
                    return resolve_res { current, node.value() };

                auto ret = node.value()->reduce();
                if (!ret)
                    return std::unexpected(ret.error());

                current = ret.value();
                if (current->inode->stat.type() != stat::type::s_ifdir)
                    return std::unexpected(error::not_a_dir);

                continue;
            }

            if (populate(current_me, segment))
                goto try_again;

            break;
        }
        return std::unexpected(error::not_found);
    }

    auto mount(std::shared_ptr<node> parent, lib::path source, lib::path target, std::string_view fsname) -> expect<void>
    {
        auto fs = find_fs(fsname);
        if (!fs)
            return std::unexpected(fs.error());

        const std::unique_lock _ { lock };

        std::shared_ptr<node> source_node;
        if (!source.empty())
        {
            auto ret = resolve(nullptr, parent, source);
            if (!ret)
                return std::unexpected(ret.error());

            source_node = ret->target;
            if (source_node->inode->stat.type() != stat::type::s_ifblk)
                return std::unexpected(error::not_a_block);
        }

        auto ret = resolve(nullptr, parent, target);
        if (!ret)
            return std::unexpected(ret.error());

        auto target_node = ret->target;

        if (target_node != node::root() && target_node->inode->stat.type() != stat::type::s_ifdir)
            return std::unexpected(error::not_a_dir);

        auto [instance, root] = fs->get()->mount(source_node);
        target_node->mountpoint = root;
        root->inode->mount = std::make_shared<struct mount>(instance, root, target_node);

        log::info("vfs: mount('{}', '{}', '{}')", source, target, fsname);

        return { };
    }

    auto unmount(std::shared_ptr<node> parent, lib::path target) -> expect<void>
    {
        lib::unused(parent, target);
        return std::unexpected(error::todo);
    }

    auto create(std::shared_ptr<node> parent, lib::path path, mode_t mode) -> expect<std::shared_ptr<node>>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(nullptr, parent, path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(nullptr, parent, path.dirname());
        if (!res)
            return std::unexpected(res.error());

        auto parent_node = res->target->me();
        const auto name = path.basename();
        auto ret = parent_node->inode->mount->fs->create(parent_node->inode, name, mode);
        if (ret)
        {
            auto node = std::make_shared<vfs::node>();
            node->parent = parent_node;
            node->name = name;
            node->inode = ret.value();
            node->inode->mount = parent_node->inode->mount;

            const std::unique_lock _ { parent_node->lock };
            parent_node->get_children()[node->name] = node;
            return node;
        }
        return std::unexpected(ret.error());
    }

    auto symlink(std::shared_ptr<node> parent, lib::path path, lib::path target) -> expect<std::shared_ptr<node>>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(nullptr, parent, path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(nullptr, parent, path.dirname());
        if (!res)
            return std::unexpected(res.error());

        auto parent_node = res->target->me();
        const auto name = path.basename();
        auto ret = parent_node->inode->mount->fs->symlink(parent_node->inode, name, target);
        if (ret)
        {
            auto node = std::make_shared<vfs::node>();
            node->parent = parent_node;
            node->name = name;
            node->inode = ret.value();
            node->inode->mount = parent_node->inode->mount;

            const std::unique_lock _ { parent_node->lock };
            parent_node->get_children()[node->name] = node;
            return node;
        }
        return std::unexpected(ret.error());
    }

    auto link(std::shared_ptr<node> parent, lib::path path, std::shared_ptr<node> tgtparent, lib::path target) -> expect<std::shared_ptr<node>>
    {
        lib::unused(parent, path, tgtparent, target);
        return std::unexpected(error::todo);
    }

    auto unlink(std::shared_ptr<node> parent, lib::path path) -> expect<void>
    {
        lib::unused(parent, path);
        return std::unexpected(error::todo);
    }

    auto stat(std::shared_ptr<node> parent, lib::path path) -> expect<::stat>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(nullptr, parent, path);
        if (!res)
            return std::unexpected(res.error());

        auto node = res->target->me();
        return node->inode->stat;
    }

    bool populate(std::shared_ptr<node> parent, std::string_view name)
    {
        const auto ret = parent->inode->mount->fs->populate(parent->inode, name);
        if (!ret.has_value())
            return false;

        const auto list = ret.value();
        if (!list.empty())
        {
            for (const auto [name, inode] : list)
            {
                auto node = std::make_shared<vfs::node>();
                node->parent = parent;
                node->name = name;
                node->inode = inode;
                node->inode->mount = parent->inode->mount;

                const std::unique_lock _ { parent->lock };
                parent->get_children()[node->name] = node;
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
            lib::ensure(mount(nullptr, "", "/", "tmpfs"));

            auto err = create(nullptr, "/dev", stat::type::s_ifdir);
            lib::ensure(err.has_value() || err.error() == error::already_exists);
            lib::ensure(mount(nullptr, "", "/dev", "devtmpfs"));
        }
    };
} // namespace vfs