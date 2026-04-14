// Copyright (C) 2024-2025  ilobilo

module system.vfs;

import system.scheduler;
import system.cpu.self;
import lib;
import std;

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
        std::unique_lock _ { lock };
        if (filesystems.contains(fs->name))
            return false;

        log::debug("vfs: registering filesystem '{}'", fs->name);
        filesystems[fs->name] = std::move(fs);
        return true;
    }

    auto find_fs(std::string_view name) -> expect<std::reference_wrapper<std::unique_ptr<filesystem>>>
    {
        std::unique_lock _ { lock };
        if (auto it = filesystems.find(name); it != filesystems.end())
            return it->second;
        return std::unexpected(error::invalid_filesystem);
    }

    std::shared_ptr<node> node::root(bool from_sched)
    {
        if (!from_sched && sched::initialised)
            return cpu::self()->sched.running_thread->proc.lock()->root;
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
            std::string_view segment { segment_view };
            if (segment.empty())
                continue;

            bool last = is_last(segment);
            auto current_me = current->me();

            if (segment == "..")
            {
                auto parent = current->parent.lock();
                if (auto root = node::root(); current == root)
                    parent = root;
                else if (current_me == current->fs->root.lock())
                    parent = current->fs->mounted_on.lock()->parent.lock();

                if (!parent)
                    return std::unexpected(error::not_found);

                if (last)
                    return resolve_res { parent, current };

                current = parent;
                current_me = current->me();
                continue;
            }

            lib::ensure(current_me->fs.get() != nullptr);

            try_again:
            if (auto it = current_me->children.find(segment); it != current_me->children.end())
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
                if (current->backing->stat.type() != stat::type::s_ifdir)
                    return std::unexpected(error::not_a_dir);

                continue;
            }

            if (current_me->fs->populate(current_me, segment))
                goto try_again;

            break;
        }
        return std::unexpected(error::not_found);
    }

    auto mount(std::shared_ptr<node> parent, lib::path source, lib::path target, std::string_view fsname) -> expect<void>
    {
        // TODO: if fsname is empty, detect fstype from source

        auto fs = find_fs(fsname);
        if (!fs)
            return std::unexpected(fs.error());

        std::unique_lock _ { lock };

        std::shared_ptr<node> source_node;
        if (!source.empty())
        {
            auto ret = resolve(parent, source);
            if (!ret)
                return std::unexpected(ret.error());

            source_node = ret->target;
            if (source_node->backing->stat.type() != stat::type::s_ifblk)
                return std::unexpected(error::not_a_block);
        }

        auto ret = resolve(parent, target);
        if (!ret)
            return std::unexpected(ret.error());

        auto target_node = ret->target;

        if (target_node != node::root() && target_node->backing->stat.type() != stat::type::s_ifdir)
            return std::unexpected(error::not_a_dir);

        auto [instance, root] = fs->get()->mount(source_node);
        target_node->mountpoint = root;
        instance->mounted_on = target_node;

        log::debug("vfs: mount('{}', '{}', '{}')", source, target, fsname);

        return { };
    }

    auto unmount(std::shared_ptr<node> parent, lib::path target) -> expect<void>
    {
        lib::unused(parent, target);
        return std::unexpected(error::todo);
    }

    auto create(std::shared_ptr<node> parent, lib::path path, mode_t mode) -> expect<std::shared_ptr<node>>
    {
        std::unique_lock _ { lock };

        auto res = resolve(parent, path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, path.dirname());
        if (!res)
            return std::unexpected(res.error());

        auto parent_node = res->target->me();
        auto ret = parent_node->fs->create(parent_node, path.basename(), mode);
        if (ret)
        {
            auto node = ret.value();
            node->fs = parent_node->fs;

            std::unique_lock _ { parent_node->lock };
            parent_node->children[node->name] = node;
            return node;
        }
        return std::unexpected(ret.error());
    }

    auto symlink(std::shared_ptr<node> parent, lib::path path, lib::path target) -> expect<std::shared_ptr<node>>
    {
        std::unique_lock _ { lock };

        auto res = resolve(parent, path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, path.dirname());
        if (!res)
            return std::unexpected(res.error());

        auto parent_node = res->target->me();
        auto ret = parent_node->fs->symlink(parent_node, path.basename(), target);
        if (ret)
        {
            auto node = ret.value();
            std::unique_lock _ { parent_node->lock };
            parent_node->children[node->name] = node;
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
        std::unique_lock _ { lock };

        auto res = resolve(parent, path);
        if (!res)
            return std::unexpected(res.error());

        auto node = res->target->me();
        return node->backing->stat;
    }
} // namespace vfs