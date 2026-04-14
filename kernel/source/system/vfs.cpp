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
            auto ret = resolve(current->parent.lock(), current->symlinked_to);
            if (!ret || ret->target == current)
                return std::unexpected(error::invalid_symlink);

            current = ret->target;
            symlink_depth--;
        }

        if (og && symlink_depth == 0 && !current->symlinked_to.empty())
            return std::unexpected(error::symloop_max);

        return current;
    }

    auto resolve(std::shared_ptr<node> parent, lib::path path) -> expect<resolve_res>
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

        if (path == "/" || path.empty())
            return resolve_res { parent, parent };

        auto current = parent;
        auto is_last = [&path](std::string_view segment) {
            return path.basename().str() == segment;
        };

        for (const auto segment_view : std::views::split(path.str(), '/'))
        {
            const std::string_view segment { segment_view };
            if (segment.empty())
                continue;

            const bool last = is_last(segment);
            auto current_me = current->me();

            if (segment == "..")
            {
                auto parent = current->parent.lock();
                if (auto root = node::root(); current == root)
                    parent = root;
                else if (current_me == current->inode->fs->root.lock())
                    parent = current->inode->fs->mounted_on.lock()->parent.lock();

                if (!parent)
                    return std::unexpected(error::not_found);

                if (last)
                    return resolve_res { parent, current };

                current = parent;
                current_me = current->me();
                continue;
            }

            lib::ensure(current_me->inode->fs.get() != nullptr);

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
            auto ret = resolve(parent, source);
            if (!ret)
                return std::unexpected(ret.error());

            source_node = ret->target;
            if (source_node->inode->stat.type() != stat::type::s_ifblk)
                return std::unexpected(error::not_a_block);
        }

        auto ret = resolve(parent, target);
        if (!ret)
            return std::unexpected(ret.error());

        auto target_node = ret->target;

        if (target_node != node::root() && target_node->inode->stat.type() != stat::type::s_ifdir)
            return std::unexpected(error::not_a_dir);

        auto [instance, root] = fs->get()->mount(source_node);
        target_node->mountpoint = root;
        instance->mounted_on = target_node;

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

        auto res = resolve(parent, path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, path.dirname());
        if (!res)
            return std::unexpected(res.error());

        auto parent_node = res->target->me();
        const auto name = path.basename();
        auto ret = parent_node->inode->fs->create(parent_node->inode, name, mode);
        if (ret)
        {
            auto node = std::make_shared<vfs::node>();
            node->parent = parent;
            node->name = name;
            node->inode = ret.value();
            node->inode->fs = parent_node->inode->fs;

            const std::unique_lock _ { parent_node->lock };
            parent_node->get_children()[node->name] = node;
            return node;
        }
        return std::unexpected(ret.error());
    }

    auto symlink(std::shared_ptr<node> parent, lib::path path, lib::path target) -> expect<std::shared_ptr<node>>
    {
        const std::unique_lock _ { lock };

        auto res = resolve(parent, path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, path.dirname());
        if (!res)
            return std::unexpected(res.error());

        auto parent_node = res->target->me();
        const auto name = path.basename();
        auto ret = parent_node->inode->fs->symlink(parent_node->inode, name, target);
        if (ret)
        {
            auto node = std::make_shared<vfs::node>();
            node->parent = parent;
            node->name = name;
            node->inode = ret.value();
            node->inode->fs = parent_node->inode->fs;

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

        auto res = resolve(parent, path);
        if (!res)
            return std::unexpected(res.error());

        auto node = res->target->me();
        return node->inode->stat;
    }

    bool populate(std::shared_ptr<node> parent, std::string_view name)
    {
        const auto ret = parent->inode->fs->populate(parent->inode, name);
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
                node->inode->fs = parent->inode->fs;

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
            lib::ensure(vfs::mount(nullptr, "", "/", "tmpfs"));

            auto err = vfs::create(nullptr, "/dev", stat::type::s_ifdir);
            lib::ensure(err.has_value() || err.error() == vfs::error::already_exists);
            lib::ensure(vfs::mount(nullptr, "", "/dev", "devtmpfs"));
        }
    };
} // namespace vfs