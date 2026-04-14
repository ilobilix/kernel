// Copyright (C) 2024-2025  ilobilo

module system.vfs;

import system.scheduler;
import system.cpu.local;
import system.dev;
import drivers.fs;
import lib;
import std;

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

        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                std::unique_ptr<filesystem>
            >, lib::mutex
        > filesystems;

        std::atomic<dev_t> next_dev = 1;
        dev_t allocate_dev()
        {
            return next_dev.fetch_add(1, std::memory_order_relaxed);
        }
    } // namespace

    filesystem::instance::instance() : dev_id { allocate_dev() } { }

    path get_root(bool absolute)
    {
        if (!absolute && sched::is_initialised())
            return sched::this_thread()->parent->root;

        path ret { .mnt = nullptr, .dentry = dentry::root(true) };
        // hmmmm is this correct? what about multiple mounts?
        if (!ret.dentry->child_mounts.empty())
            ret.mnt = ret.dentry->child_mounts.front().lock();
        return ret;
    }

    bool register_fs(std::unique_ptr<filesystem> fs)
    {
        auto locked = filesystems.lock();
        if (locked->contains(fs->name))
            return false;

        lib::info("vfs: registering filesystem '{}'", fs->name);
        locked.value()[fs->name] = std::move(fs);
        return true;
    }

    auto find_fs(std::string_view name) -> expect<std::reference_wrapper<std::unique_ptr<filesystem>>>
    {
        auto locked = filesystems.lock();
        if (auto it = locked->find(name); it != locked->end())
            return it->second;
        return std::unexpected(error::invalid_filesystem);
    }

    std::shared_ptr<dentry> dentry::root(bool absolute)
    {
        if (!absolute && sched::is_initialised())
            return sched::this_thread()->parent->root.dentry;
        return vfs::root;
    }

    std::string pathname_from(path path)
    {
        std::size_t len = 0;
        std::vector<std::string_view> segments;
        while (true)
        {
            while (path.dentry == path.mnt->root)
                path = path.mnt->mounted_on.value();

            if (path.dentry == vfs::root)
                break;

            segments.push_back(path.dentry->name);
            len += path.dentry->name.size();
            path.dentry = path.dentry->parent.lock();
        }
        std::string result;
        result.reserve(std::max(1zu, len + segments.size()));
        for (auto it = segments.rbegin(); it != segments.rend(); it++)
        {
            result += '/';
            result += *it;
        }
        if (result.empty())
            result = "/";
        return result;
    }

    auto path_for(lib::path _path) -> expect<path>
    {
        std::optional<path> parent { };
        if (sched::is_initialised())
            parent = sched::this_thread()->parent->root;
        auto res = resolve(parent, _path);
        if (!res)
            return std::unexpected(res.error());
        return res->target;
    }

    auto resolve(std::optional<path> parent, lib::path _path) -> expect<resolve_res>
    {
        if (!parent || _path.is_absolute())
            parent = get_root(false);

        lib::bug_on(!parent.has_value());

        if (_path == "/" || _path.empty() || _path == ".")
            return resolve_res { parent.value(), parent.value() };

        lib::bug_on(parent->mnt == nullptr);

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
            {
                const auto &rlocked = current.dentry->children.read_lock();
                if (auto it = rlocked->find(segment); it != rlocked->end())
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

                    if (next.dentry->inode->stat.type() == stat::type::s_iflnk)
                    {
                        const auto reduced = reduce(current, next);
                        if (!reduced)
                            return std::unexpected(reduced.error());
                        next = reduced.value();
                    }

                    if (next.dentry->inode->stat.type() != stat::type::s_ifdir)
                        return std::unexpected(error::not_a_dir);

                    current = next;
                    continue;
                }
            }

            if (populate(current, segment))
                goto try_again;

            break;
        }
        return std::unexpected(error::not_found);
    }

    auto reduce(path parent, path src, std::size_t symlink_depth) -> expect<path>
    {
        const auto is_symlink = [&src]
        {
            const auto &dentry = src.dentry;
            return
                dentry->inode->stat.type() == stat::type::s_iflnk &&
                !dentry->symlinked_to.empty();
        };

        const auto og = symlink_depth;
        while (symlink_depth > 0)
        {
            if (!is_symlink())
                return src;

            const auto ret = resolve(parent, src.dentry->symlinked_to);
            if (!ret || ret->target.dentry == src.dentry)
                return std::unexpected(error::invalid_symlink);

            parent = ret->parent;
            src = ret->target;
            symlink_depth--;
        }

        if (og && symlink_depth == 0 && is_symlink())
            return std::unexpected(error::symloop_max);

        return src;
    }

    auto mount(lib::path source_path, lib::path target_path, std::string_view fstype, int flags) -> expect<void>
    {
        // TODO
        lib::unused(flags);

        auto fs = find_fs(fstype);
        if (!fs)
            return std::unexpected(fs.error());

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

        auto mnt = fs->get()->mount(source ? source->dentry : std::shared_ptr<vfs::dentry> { });
        if (!mnt)
            return std::unexpected(mnt.error());

        mnt.value()->mounted_on = target;
        target.dentry->child_mounts.push_back(mnt.value());

        lib::info("vfs: mount('{}', '{}', '{}')", source_path, target_path, fstype);

        return { };
    }

    auto unmount(lib::path target) -> expect<void>
    {
        lib::unused(target);
        return std::unexpected(error::todo);
    }

    auto create(std::optional<path> parent, lib::path _path, mode_t mode, dev_t rdev) -> expect<path>
    {
        std::shared_ptr<vfs::ops> ops { };
        if (rdev != 0)
        {
            const auto type = stat::type(mode);
            if (type != stat::type::s_ifchr && type != stat::type::s_ifblk)
                return std::unexpected(error::invalid_type);
            ops = dev::get_cdev_ops(rdev);
            if (!ops)
                return std::unexpected(error::invalid_device);
        }

        auto res = resolve(parent, _path);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, _path.dirname());
        if (!res)
            return std::unexpected(res.error());

        const auto real_parent = res->target;
        const auto name = _path.basename();

        auto ret = real_parent.mnt->fs.lock()->create(real_parent.dentry->inode, name, mode, rdev, ops);
        if (ret)
        {
            auto dentry = std::make_shared<vfs::dentry>();
            dentry->parent = real_parent.dentry;
            dentry->name = name;
            dentry->inode = ret.value();

            real_parent.dentry->children.write_lock().value()[dentry->name] = dentry;
            return path { real_parent.mnt, dentry };
        }
        return std::unexpected(ret.error());
    }

    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> expect<path>
    {
        auto res = resolve(parent, src);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, src.dirname());
        if (!res)
            return std::unexpected(res.error());

        const auto real_parent = res->target;
        const auto name = src.basename();

        auto ret = real_parent.mnt->fs.lock()->symlink(real_parent.dentry->inode, name, target);
        if (ret)
        {
            auto dentry = std::make_shared<vfs::dentry>();
            dentry->parent = real_parent.dentry;
            dentry->name = name;
            dentry->symlinked_to = target.str();
            dentry->inode = ret.value();

            real_parent.dentry->children.write_lock().value()[dentry->name] = dentry;
            return path { real_parent.mnt, dentry };
        }
        return std::unexpected(ret.error());
    }

    auto link(std::optional<path> parent, lib::path src, std::optional<path> tgtparent, lib::path target, bool follow_links) -> expect<path>
    {
        auto res = resolve(parent, src);
        if (res)
            return std::unexpected(error::already_exists);

        res = resolve(parent, src.dirname());
        if (!res)
            return std::unexpected(res.error());

        const auto real_parent = res->target;
        const auto name = src.basename();

        res = resolve(tgtparent, target);
        if (!res)
            return std::unexpected(res.error());

        const auto tgt = res->target;
        const auto type = tgt.dentry->inode->stat.type();
        if (follow_links && type == stat::s_iflnk)
        {
            const auto reduced = reduce(res->parent, res->target);
            if (!reduced)
                return std::unexpected(reduced.error());
            res->target = reduced.value();
        }

        if (tgt.dentry->inode->stat.type() == stat::type::s_ifdir)
            return std::unexpected(error::target_is_a_dir);

        if (real_parent.mnt != tgt.mnt)
            return std::unexpected(error::different_filesystem);

        auto ret = real_parent.mnt->fs.lock()->link(real_parent.dentry->inode, name, tgt.dentry->inode);
        if (ret)
        {
            auto dentry = std::make_shared<vfs::dentry>();
            dentry->parent = real_parent.dentry;
            dentry->name = name;
            dentry->inode = ret.value();

            real_parent.dentry->children.write_lock().value()[dentry->name] = dentry;
            return path { real_parent.mnt, dentry };
        }
        return std::unexpected(ret.error());
    }

    auto unlink(std::optional<path> parent, lib::path path) -> expect<void>
    {
        const auto res = resolve(parent, path);
        if (!res)
            return std::unexpected(res.error());

        const auto real_parent = res->parent.dentry;
        const auto name = path.basename();

        if (res->target.dentry->inode->stat.type() == stat::s_ifdir)
        {
            if (res->target.dentry == res->target.mnt->root)
                return std::unexpected(error::target_is_busy);

            if (!res->target.dentry->children.read_lock()->empty())
                return std::unexpected(error::dir_not_empty);
            populate(res->target);
            if (!res->target.dentry->children.read_lock()->empty())
                return std::unexpected(error::dir_not_empty);
        }

        auto ret = res->target.mnt->fs.lock()->unlink(res->target.dentry->inode);
        if (ret)
        {
            auto wlocked = real_parent->children.write_lock();
            auto it = wlocked->find(name);
            lib::bug_on(it == wlocked->end());
            wlocked->erase(it);
            return { };
        }
        return std::unexpected(ret.error());
    }

    bool check_access(uid_t uid, gid_t gid, const std::span<const gid_t> &supgids, const ::stat &stat, int mode)
    {
        if (mode == f_ok)
            return true;

        if (uid == 0)
        {
            if (!(mode & x_ok))
                return true;

            if (stat.type() == ::stat::s_ifdir)
                return true;

            return (stat.st_mode & (s_ixusr | s_ixgrp | s_ixoth)) != 0;
        }

        auto rbit = s_iroth;
        auto wbit = s_iwoth;
        auto xbit = s_ixoth;

        if (uid == stat.st_uid)
        {
            rbit = s_irusr;
            wbit = s_iwusr;
            xbit = s_ixusr;
        }
        else
        {
            bool is_group = (gid == stat.st_gid);
            if (!is_group)
            {
                for (const auto &supgid : supgids)
                {
                    if (supgid == stat.st_gid)
                    {
                        is_group = true;
                        break;
                    }
                }
            }

            if (is_group)
            {
                rbit = s_irgrp;
                wbit = s_iwgrp;
                xbit = s_ixgrp;
            }
        }

        if ((mode & r_ok) && !(stat.st_mode & rbit))
            return false;
        if ((mode & w_ok) && !(stat.st_mode & wbit))
            return false;
        if ((mode & x_ok) && !(stat.st_mode & xbit))
            return false;

        return true;
    }

    bool populate(path parent, std::string_view name)
    {
        const auto ret = parent.mnt->fs.lock()->populate(parent.dentry->inode, name);
        if (!ret.has_value())
            return false;

        const auto list = ret.value();
        if (!list.empty())
        {
            auto wlocked = parent.dentry->children.write_lock();
            for (const auto [name, inode] : list)
            {
                auto dentry = std::make_shared<vfs::dentry>();
                dentry->parent = parent.dentry;
                dentry->name = name;
                dentry->inode = inode;

                wlocked.value()[dentry->name] = dentry;
            }
            return true;
        }
        return false;
    }

    lib::initgraph::stage *root_mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.root-mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task root_task
    {
        "vfs.mount-root",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::registered_stage() },
        lib::initgraph::entail { root_mounted_stage() },
        [] {
            lib::bug_on(!mount("", "/", "tmpfs", 0));

            const auto pid0 = sched::proc_for(0);
            pid0->root = pid0->cwd = get_root(true);
        }
    };
} // namespace vfs