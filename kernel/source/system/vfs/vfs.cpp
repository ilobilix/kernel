// Copyright (C) 2024-2026  ilobilo

module system.vfs;

import system.cpu.local;
import system.vfs.dev;
import system.sched;
import drivers.fs;
import magic_enum;
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
            root->inode = std::make_shared<inode>();
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

        path resolve_mounts(path path)
        {
            while (path.mnt != nullptr && path.dentry == path.mnt->root)
            {
                if (!path.mnt->mounted_on.has_value())
                    break;
                path = path.mnt->mounted_on.value();
            }
            return path;
        };
    } // namespace

    filesystem::instance::instance() : dev_id { allocate_dev() } { }

    auto file::get_ops() const -> lib::expect<std::shared_ptr<ops>>
    {
        lib::bug_on(!path.dentry || !path.dentry->inode);

        auto &inode = path.dentry->inode;
        const auto &stat = inode->stat;

        const auto dev = stat.st_dev;
        const auto rdev = stat.st_rdev;
        const auto mode = stat.st_mode;

        auto ops = dev::get_ops(dev, rdev, mode);
        if (!ops.has_value())
            return std::unexpected { ops.error() };

        return *ops;
    }

    lib::expect<std::size_t> file::getdents(lib::maybe_uspan<std::byte> buffer)
    {
        const auto ops = get_ops();
        if (!ops.has_value())
            return std::unexpected { ops.error() };

        const std::unique_lock _ { lock };
        std::size_t progress = 0;
        if (offset < 3)
        {
            if (offset < 2)
            {
                static const std::span<std::byte> dotstr {
                    reinterpret_cast<std::byte *>(const_cast<char *>(".")), 2
                };
                static const std::size_t reclen = (sizeof(vfs::dirent64) + dotstr.size() + 7) & ~7;

                if (progress + reclen > buffer.size())
                    return std::unexpected { lib::err::buffer_too_small };

                const auto ino = path.dentry->inode->stat.st_ino;
                vfs::dirent64 dot
                {
                    .d_ino = ino,
                    .d_off = 2,
                    .d_reclen = static_cast<std::uint16_t>(reclen),
                    .d_type = vfs::stat_to_dt(stat::type::s_ifdir)
                };

                buffer.subspan(progress, sizeof(vfs::dirent64))
                    .copy_from(reinterpret_cast<std::byte *>(&dot));
                buffer.subspan(progress + sizeof(vfs::dirent64), 2)
                    .copy_from(dotstr);

                progress += reclen;
                offset = 2;
            }

            if (offset == 2)
            {
                static const std::span<std::byte> dotdotstr {
                    reinterpret_cast<std::byte *>(const_cast<char *>("..")), 3
                };
                static const std::size_t reclen = (sizeof(vfs::dirent64) + dotdotstr.size() + 7) & ~7;

                if (progress + reclen > buffer.size())
                {
                    if (progress == 0)
                        return std::unexpected { lib::err::buffer_too_small };
                    return progress;
                }

                const auto parent_ino = resolve_mounts(path).dentry->parent.lock()->inode->stat.st_ino;

                vfs::dirent64 dotdot
                {
                    .d_ino = parent_ino,
                    .d_off = 3,
                    .d_reclen = static_cast<std::uint16_t>(reclen),
                    .d_type = vfs::stat_to_dt(stat::type::s_ifdir)
                };

                buffer.subspan(progress, sizeof(vfs::dirent64))
                    .copy_from(reinterpret_cast<std::byte *>(&dotdot));
                buffer.subspan(progress + sizeof(vfs::dirent64), 3)
                    .copy_from(dotdotstr);

                progress += reclen;
                offset++;
            }
        }

        auto fs = path.mnt->fs.lock();
        while (true)
        {
            auto batch = fs->readdir(path.dentry, offset);
            if (!batch)
                return std::unexpected { batch.error() };

            if (batch->empty())
                break;

            for (const auto &entry : *batch)
            {
                const auto reclen = (sizeof(vfs::dirent64) + entry.name.size() + 1 + 7) & ~7;

                if (progress + reclen > buffer.size())
                {
                    if (progress == 0)
                        return std::unexpected { lib::err::buffer_too_small };
                    return progress;
                }

                vfs::dirent64 dirent
                {
                    .d_ino = entry.inode->stat.st_ino,
                    .d_off = static_cast<std::int64_t>(entry.cookie + 1),
                    .d_reclen = static_cast<std::uint16_t>(reclen),
                    .d_type = vfs::stat_to_dt(entry.inode->stat.type())
                };

                buffer.subspan(progress, sizeof(dirent))
                    .copy_from(reinterpret_cast<std::byte *>(&dirent));

                buffer.subspan(progress + sizeof(dirent), entry.name.size() + 1)
                    .copy_from(reinterpret_cast<const std::byte *>(entry.name.c_str()));

                progress += reclen;
                offset = entry.cookie + 1;
            }
        }

        return progress;
    }

    auto filesystem::instance::lookup(std::shared_ptr<dentry> dir, std::string_view name)
        -> lib::expect<std::optional<dir_entry>>
    {
        if (auto den = dir->children.read_lock()->lookup(name))
            return dir_entry { std::string { name }, den->inode, 0 };

        std::size_t cookie = 3;
        while (true)
        {
            auto batch = readdir(dir, cookie);
            if (!batch)
                return std::unexpected { batch.error() };

            if (batch->empty())
                return std::nullopt;

            auto wlocked = dir->children.write_lock();
            for (auto &entry : *batch)
            {
                if (!wlocked->lookup(entry.name))
                {
                    auto dentry = std::make_shared<vfs::dentry>();
                    dentry->parent = dir;
                    dentry->name = entry.name;
                    dentry->inode = entry.inode;
                    wlocked->insert(dentry);
                }

                if (entry.name == name)
                    return entry;

                cookie = entry.cookie + 1;
            }
        }
    }

    path get_root(bool absolute)
    {
        if (!absolute)
        {
            auto proc = sched::current_process();
            if (proc->vfs) [[likely]]
                return proc->vfs->root;
        }

        path ret { .mnt = nullptr, .dentry = dentry::root(true) };
        while (!ret.dentry->child_mounts.empty())
        {
            const auto mnt = ret.dentry->child_mounts.back().lock();
            if (!mnt || !mnt->root)
                break;

            ret.mnt = mnt;
            ret.dentry = mnt->root;
        }
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

    auto find_fs(std::string_view name)
        -> lib::expect<std::reference_wrapper<std::unique_ptr<filesystem>>>
    {
        auto locked = filesystems.lock();
        if (auto it = locked->find(name); it != locked->end())
            return it->second;
        return std::unexpected { lib::err::invalid_filesystem };
    }

    std::shared_ptr<dentry> dentry::root(bool absolute)
    {
        if (!absolute)
        {
            auto proc = sched::current_process();
            if (proc->vfs) [[likely]]
                return proc->vfs->root.dentry;
        }
        return vfs::root;
    }

    std::string pathname_from(path path)
    {
        std::size_t len = 0;
        std::vector<std::string_view> segments;

        while (true)
        {
            path = resolve_mounts(path);
            if (path.dentry == nullptr || path.dentry == vfs::root)
                break;

            auto parent = path.dentry->parent.lock();
            if (path.dentry == parent)
                break;

            segments.push_back(path.dentry->name);
            len += path.dentry->name.size();
            path.dentry = parent;
        }

        std::string result;
        result.reserve(len + segments.size());

        for (auto it = segments.rbegin(); it != segments.rend(); it++)
        {
            result += '/';
            result += *it;
        }

        if (result.empty())
            result = "/";
        return result;
    }

    auto path_for(lib::path _path) -> lib::expect<path>
    {
        // hmmm is this correct?
        auto res = resolve(std::nullopt, _path);
        if (!res)
            return std::unexpected { res.error() };
        return res->target;
    }

    auto resolve(std::optional<path> parent, lib::path _path, bool automount) -> lib::expect<resolve_res>
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

            const bool last = (i == size);

            if (segment == "..")
            {
                auto proc_root = get_root(false);
                if (current.dentry == proc_root.dentry && current.mnt == proc_root.mnt)
                {
                    if (last)
                        return resolve_res { current, current };
                    continue;
                }

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

            auto dentry = current.dentry->children.read_lock()->lookup(segment);
            if (dentry == nullptr)
            {
                auto found = current.mnt->fs.lock()->lookup(current.dentry, segment);
                if (!found)
                    return std::unexpected { found.error() };

                if (!found->has_value())
                    return std::unexpected { lib::err::not_found };

                dentry = current.dentry->children.read_lock()->lookup(segment);
                if (dentry == nullptr)
                {
                    const auto &entry = *found;

                    auto wlocked = current.dentry->children.write_lock();
                    dentry = wlocked->lookup(entry->name);
                    if (dentry == nullptr)
                    {
                        dentry = std::make_shared<vfs::dentry>();
                        dentry->parent = current.dentry;
                        dentry->name = entry->name;
                        dentry->inode = entry->inode;
                        wlocked->insert(dentry);
                    }
                }
            }

            auto mnt = current.mnt;

            again:
            if (automount || !last)
            {
                for (const auto &child_mnt : dentry->child_mounts)
                {
                    const auto locked = child_mnt.lock();
                    if (!locked || !locked->mounted_on.has_value() || !locked->root)
                        continue;

                    if (mnt == locked->mounted_on->mnt)
                    {
                        mnt = locked;
                        dentry = locked->root;
                        goto again;
                    }
                }
            }

            path next { mnt, dentry };

            if (last)
                return resolve_res { current, next };

            auto type = next.dentry->inode->stat.type();
            if (type == stat::type::s_iflnk)
            {
                const auto reduced = reduce(current, next);
                if (!reduced)
                    return std::unexpected { reduced.error() };
                next = reduced.value();
                type = next.dentry->inode->stat.type();
            }

            if (type != stat::type::s_ifdir)
                return std::unexpected { lib::err::not_a_dir };

            current = next;
        }

        return std::unexpected { lib::err::not_found };
    }

    auto reduce(path parent, path src, bool automount, std::size_t symlink_depth) -> lib::expect<path>
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

            const auto ret = resolve(parent, src.dentry->symlinked_to, automount);
            if (!ret || ret->target.dentry == src.dentry)
                return std::unexpected { lib::err::invalid_symlink };

            parent = ret->parent;
            src = ret->target;
            symlink_depth--;
        }

        if (og && symlink_depth == 0 && is_symlink())
            return std::unexpected { lib::err::symloop_max };

        return src;
    }

    auto mount(lib::path source_path, lib::path target_path, std::string_view fstype, int flags) -> lib::expect<void>
    {
        // TODO
        lib::unused(flags);

        auto fs = find_fs(fstype);
        if (!fs)
            return std::unexpected { fs.error() };

        std::optional<path> source { };
        if (!source_path.empty())
        {
            auto ret = path_for(source_path);
            if (!ret)
                return std::unexpected { ret.error() };

            source = ret.value();
            if (source->dentry->inode->stat.type() != stat::type::s_ifblk)
                return std::unexpected { lib::err::not_a_block };
        }

        auto ret = path_for(target_path);
        if (!ret)
            return std::unexpected { ret.error() };

        auto target = ret.value();
        if (target.dentry->inode->stat.type() != stat::type::s_ifdir)
            return std::unexpected { lib::err::not_a_dir };

        auto mnt = fs->get()->mount(source ? source->dentry : std::shared_ptr<vfs::dentry> { });
        if (!mnt)
            return std::unexpected { mnt.error() };

        mnt.value()->mounted_on = target;
        target.dentry->child_mounts.push_back(mnt.value());

        lib::info("vfs: mount('{}', '{}', '{}')", source_path, target_path, fstype);

        return { };
    }

    auto unmount(lib::path target) -> lib::expect<void>
    {
        lib::unused(target);
        return std::unexpected { lib::err::todo };
    }

    auto create(std::optional<path> parent, lib::path _path, mode_t mode, dev_t rdev) -> lib::expect<path>
    {
        auto res = resolve(parent, _path);
        if (res)
            return std::unexpected { lib::err::already_exists };

        res = resolve(parent, _path.dirname());
        if (!res)
            return std::unexpected { res.error() };

        const auto real_parent = res->target;
        const auto name = _path.basename();

        auto ret = real_parent.mnt->fs.lock()->create(real_parent.dentry->inode, name, mode, rdev);
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = std::make_shared<vfs::dentry>();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.write_lock()->insert(dentry);
        return path { real_parent.mnt, dentry };
    }

    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> lib::expect<path>
    {
        auto res = resolve(parent, src);
        if (res)
            return std::unexpected { lib::err::already_exists };

        res = resolve(parent, src.dirname());
        if (!res)
            return std::unexpected { res.error() };

        const auto real_parent = res->target;
        const auto name = src.basename();

        auto ret = real_parent.mnt->fs.lock()->symlink(real_parent.dentry->inode, name, target);
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = std::make_shared<vfs::dentry>();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->symlinked_to = target.str();
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.write_lock()->insert(dentry);
        return path { real_parent.mnt, dentry };
    }

    auto link(
        std::optional<path> parent, lib::path src,
        std::optional<path> tgtparent, lib::path target, bool follow_links
    ) -> lib::expect<path>
    {
        auto res = resolve(parent, src);
        if (res)
            return std::unexpected { lib::err::already_exists };

        res = resolve(parent, src.dirname());
        if (!res)
            return std::unexpected { res.error() };

        const auto real_parent = std::move(res->target);
        const auto name = src.basename();

        res = resolve(tgtparent, target);
        if (!res)
            return std::unexpected { res.error() };

        auto tgt = std::move(res->target);
        if (follow_links && tgt.dentry->inode->stat.type() == stat::s_iflnk)
        {
            auto reduced = reduce(res->parent, tgt);
            if (!reduced)
                return std::unexpected { reduced.error() };
            tgt = std::move(*reduced);
        }

        if (tgt.dentry->inode->stat.type() == stat::type::s_ifdir)
            return std::unexpected { lib::err::target_is_a_dir };

        if (real_parent.mnt != tgt.mnt)
            return std::unexpected { lib::err::different_filesystem };

        auto ret = real_parent.mnt->fs.lock()->link(
            real_parent.dentry->inode,
            name,
            tgt.dentry->inode
        );
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = std::make_shared<vfs::dentry>();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.write_lock()->insert(dentry);
        return path { real_parent.mnt, dentry };
    }

    auto unlink(std::optional<path> parent, lib::path path) -> lib::expect<void>
    {
        const auto res = resolve(parent, path);
        if (!res)
            return std::unexpected { res.error() };

        const auto real_parent = res->parent.dentry;
        const auto name = path.basename();

        auto &inode = res->target.dentry->inode;
        if (inode->stat.type() == stat::s_ifdir)
        {
            if (res->target.dentry == res->target.mnt->root)
                return std::unexpected { lib::err::target_is_busy };

            if (!res->target.dentry->children.read_lock()->empty())
                return std::unexpected { lib::err::dir_not_empty };

            std::size_t cookie = 3;
            const auto ret = res->target.mnt->fs.lock()->readdir(res->target.dentry, cookie);
            if (!ret)
                return std::unexpected { ret.error() };
            if (!ret->empty())
                return std::unexpected { lib::err::dir_not_empty };
        }

        const auto ret = res->target.mnt->fs.lock()->unlink(inode);
        if (!ret)
            return std::unexpected { ret.error() };

        auto wlocked = real_parent->children.write_lock();
        lib::bug_on(!wlocked->erase(name));
        return { };
    }

    bool fdtable::close(int fd)
    {
        auto fdesc = get(fd);
        if (!fdesc)
            return false;

        if (!fds.write_lock()->erase(fd))
            return false;

        if (fdesc->file && fdesc->file->ref.fetch_sub(1) == 1)
        {
            if (const auto ret = fdesc->file->close(); !ret)
                lib::error("failed to close fd: {}", magic_enum::enum_name(ret.error()));
        }

        return true;
    }

    std::shared_ptr<vfs::filedesc> fdtable::get(int fd)
    {
        const auto rlocked = fds.read_lock();
        auto it = rlocked->find(fd);
        if (it == rlocked->end())
            return nullptr;
        return it->second;
    }

    int fdtable::alloc(std::shared_ptr<vfs::filedesc> desc, int fd, bool force)
    {
        auto wlocked = fds.write_lock();
        if (wlocked->contains(fd))
        {
            if (!force)
            {
                fd = next_fd++;
                while (wlocked->contains(fd))
                    fd++;
                next_fd = fd + 1;
            }
            else lib::bug_on(!wlocked->erase(fd));
        }
        else if (fd >= next_fd)
            next_fd = fd + 1;

        wlocked.value()[fd] = std::move(desc);
        return fd;
    }

    int fdtable::dup(int oldfd, int newfd, bool closexec, bool force)
    {
        if (oldfd < 0 || newfd < 0)
            return (errno = EBADF, -1);
        auto fdesc = get(oldfd);
        if (!fdesc)
            return (errno = EBADF, -1);

        auto newfdesc = std::make_shared<vfs::filedesc>(fdesc->file, closexec);
        const auto fd = alloc(std::move(newfdesc), newfd, force);
        if (fd < 0)
            return (errno = EMFILE, -1);
        fdesc->file->ref.fetch_add(1);
        return fd;
    }

    void fdtable::close_on_exec()
    {
        auto wlocked = fds.write_lock();
        for (auto it = wlocked->begin(); it != wlocked->end(); )
        {
            if (it->second && it->second->closexec.load(std::memory_order_relaxed))
            {
                auto &fdesc = it->second;
                if (fdesc->file && fdesc->file->ref.fetch_sub(1) == 1)
                {
                    if (const auto ret = fdesc->file->close(); !ret)
                        lib::error("failed to close fd: {}", magic_enum::enum_name(ret.error()));
                }
                it = wlocked->erase(it);
            }
            else it++;
        }
    }

    fdtable::fdtable(fdtable &other) : next_fd { other.next_fd }
    {
        auto orlocked = other.fds.read_lock();
        auto wlocked = fds.write_lock();

        for (const auto &[fd, old_desc] : *orlocked)
        {
            if (!old_desc)
                continue;

            old_desc->file->ref.fetch_add(1, std::memory_order_relaxed);
            wlocked.value()[fd] = std::make_shared<vfs::filedesc>(
                old_desc->file,
                old_desc->closexec.load(std::memory_order_relaxed)
            );
        }
    }

    fdtable::~fdtable()
    {
        auto wlocked = fds.write_lock();
        for (auto &[fd, fdesc] : *wlocked)
        {
            if (fdesc && fdesc->file && fdesc->file->ref.fetch_sub(1) == 1)
            {
                if (const auto ret = fdesc->file->close(); !ret)
                    lib::error("failed to close fd: {}", magic_enum::enum_name(ret.error()));
            }
        }
        wlocked->clear();
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
        }
    };
} // namespace vfs
