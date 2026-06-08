// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.chrono;

namespace syscall::vfs
{
    using namespace ::vfs;

    std::ssize_t readlinkat(
        int dirfd, const char __user *pathname, char __user *buf, std::size_t bufsiz
    )
    {
        const auto proc = sched::current_process();

        const auto target = get_target(proc, dirfd, pathname, false, true, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (target->dentry->inode->stat.type() != stat::type::s_iflnk)
            return -EINVAL;

        if (!target->mnt)
            return -EINVAL;
        auto fs = target->mnt->fs.lock();
        if (fs.get() == nullptr)
            return -EIO;

        const auto link_res = fs->readlink(target->dentry);
        if (!link_res)
            return -lib::map_error(link_res.error());

        const auto &link = *link_res;
        const auto to_copy = static_cast<std::size_t>(std::min<std::size_t>(bufsiz, link.size()));

        if (to_copy > 0)
        {
            if (!lib::copy_to_user(buf, link.data(), to_copy))
                return -EFAULT;
        }

        return static_cast<std::ssize_t>(to_copy);
    }

    std::ssize_t readlink(const char __user *pathname, char __user *buf, std::size_t bufsiz)
    {
        return readlinkat(at_fdcwd, pathname, buf, bufsiz);
    }

    int mkdirat(int dirfd, const char __user *pathname, mode_t mode)
    {
        const auto proc = sched::current_process();

        auto val = detail::get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto path = std::move(*val);
        if (detail::resolve_from(proc, dirfd, path).has_value())
            return -EEXIST;

        const auto parent = detail::resolve_parent_dir(proc, dirfd, path.dirname());
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        const auto &parent_stat = parent->dentry->inode->stat;
        if (parent_stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (detail::readonly_mount(*parent))
            return -EROFS;

        if (!sched::check_perms(proc->cred, parent_stat, sched::access_mode::write))
            return -EACCES;

        const auto cmode = (mode & ~proc->vfs->umask) | stat::type::s_ifdir;
        auto created = create(*parent, path.basename(), cmode);
        if (!created.has_value())
            return -lib::map_error(created.error());

        {
            const std::unique_lock _ { created->dentry->inode->lock };

            auto &stat = created->dentry->inode->stat;
            stat.st_uid = proc->cred->euid;

            if (parent_stat.st_mode & s_isgid)
                stat.st_gid = parent_stat.st_gid;
            else
                stat.st_gid = proc->cred->egid;

            if (const auto ret = dirty_inode(*created); !ret)
                return -lib::map_error(ret.error());
        }
        return 0;
    }

    int mkdir(const char __user *pathname, mode_t mode)
    {
        return mkdirat(at_fdcwd, pathname, mode);
    }

    int unlinkat(int dirfd, const char __user *pathname, int flags)
    {
        if (flags & ~at_removedir)
            return -EINVAL;

        const auto proc = sched::current_process();

        auto val = detail::get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto path = std::move(*val);

        const auto parent = detail::resolve_parent_dir(proc, dirfd, path.dirname());
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        if (detail::readonly_mount(*parent))
            return -EROFS;

        const auto &parent_stat = parent->dentry->inode->stat;
        if (!sched::check_perms(proc->cred, parent_stat, sched::access_mode::write))
            return -EACCES;

        const auto target = get_target(proc, dirfd, pathname, false, false, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto &tstat = target->dentry->inode->stat;
        const bool is_dir = tstat.type() == stat::type::s_ifdir;
        if (flags & at_removedir)
        {
            if (!is_dir)
                return -ENOTDIR;
        }
        else if (is_dir)
            return -EISDIR;

        if ((parent_stat.st_mode & s_isvtx) != 0)
        {
            const auto &cred = proc->cred;
            if (cred->fsuid != tstat.st_uid && cred->fsuid != parent_stat.st_uid &&
                !sched::capable(cred, sched::cap_t::fowner))
                return -EACCES;
        }

        if (const auto ret = unlink(*parent, path.basename()); !ret)
            return -lib::map_error(ret.error());

        return 0;
    }

    int unlink(const char __user *pathname) { return unlinkat(at_fdcwd, pathname, 0); }

    int rmdir(const char __user *pathname) { return unlinkat(at_fdcwd, pathname, at_removedir); }

    int mknodat(int dirfd, const char __user *pathname, mode_t mode, dev_t dev)
    {
        auto kind = stat::type(mode);
        if (static_cast<int>(kind) == 0)
            kind = stat::type::s_ifreg;

        switch (kind)
        {
            case stat::type::s_ifreg:
            case stat::type::s_ifchr:
            case stat::type::s_ifblk:
            case stat::type::s_ififo:
            case stat::type::s_ifsock:
                break;
            default:
                return -EINVAL;
        }

        const auto proc = sched::current_process();
        const auto &cred = proc->cred;

        const bool is_dev = (kind == stat::type::s_ifchr || kind == stat::type::s_ifblk);
        if (is_dev && !sched::capable(cred, sched::cap_t::mknod))
            return -EPERM;

        auto val = detail::get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto path = std::move(*val);
        if (detail::resolve_from(proc, dirfd, path).has_value())
            return -EEXIST;

        const auto parent = detail::resolve_parent_dir(proc, dirfd, path.dirname());
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        const auto &parent_stat = parent->dentry->inode->stat;
        if (parent_stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (detail::readonly_mount(*parent))
            return -EROFS;

        if (!sched::check_perms(cred, parent_stat, sched::access_mode::write))
            return -EACCES;

        const auto perm = (mode & ~proc->vfs->umask) &
            (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

        auto created = create(*parent, path.basename(), perm | kind, is_dev ? dev : 0);
        if (!created.has_value())
            return -lib::map_error(created.error());

        {
            const std::unique_lock _ { created->dentry->inode->lock };

            auto &stat = created->dentry->inode->stat;
            stat.st_uid = cred->euid;
            if (parent_stat.st_mode & s_isgid)
                stat.st_gid = parent_stat.st_gid;
            else
                stat.st_gid = cred->egid;

            if (const auto ret = dirty_inode(*created); !ret)
                return -lib::map_error(ret.error());
        }
        return 0;
    }

    int mknod(const char __user *pathname, mode_t mode, dev_t dev)
    {
        return mknodat(at_fdcwd, pathname, mode, dev);
    }

    int linkat(
        int olddirfd, const char __user *oldpath, int newdirfd, const char __user *newpath,
        int flags
    )
    {
        if (flags & ~(at_symlink_follow | at_empty_path))
            return -EINVAL;

        const auto proc = sched::current_process();
        const bool follow_links = (flags & at_symlink_follow) != 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto src = get_target(proc, olddirfd, oldpath, follow_links, empty_path, true);
        if (!src.has_value())
            return -lib::map_error(src.error());

        auto new_val = detail::get_path(newpath);
        if (!new_val.has_value())
            return -lib::map_error(new_val.error());
        const auto new_path = std::move(*new_val);

        const auto new_anchor = detail::get_parent(proc, newdirfd, new_path);
        if (!new_anchor.has_value())
            return -lib::map_error(new_anchor.error());

        const auto new_parent = detail::resolve_parent_dir(proc, newdirfd, new_path.dirname());
        if (!new_parent.has_value())
            return -lib::map_error(new_parent.error());

        if (detail::readonly_mount(*new_parent))
            return -EROFS;

        const auto &new_parent_stat = new_parent->dentry->inode->stat;
        if (new_parent_stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (!sched::check_perms(proc->cred, new_parent_stat, sched::access_mode::write))
            return -EACCES;

        if (const auto ret = link(*new_anchor, new_path, *src, lib::path { }, false); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int link(const char __user *oldpath, const char __user *newpath)
    {
        return linkat(at_fdcwd, oldpath, at_fdcwd, newpath, 0);
    }

    int symlinkat(const char __user *target, int newdirfd, const char __user *linkpath)
    {
        const auto proc = sched::current_process();

        auto target_val = detail::get_path(target);
        if (!target_val.has_value())
            return -lib::map_error(target_val.error());

        auto link_val = detail::get_path(linkpath);
        if (!link_val.has_value())
            return -lib::map_error(link_val.error());

        const auto link = std::move(*link_val);

        const auto parent = detail::get_parent(proc, newdirfd, link);
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        const auto parent_res = resolve(*parent, link.dirname());
        if (!parent_res.has_value())
            return -lib::map_error(parent_res.error());

        auto parent_dir = parent_res->target;
        if (parent_dir.dentry->inode->stat.type() == stat::type::s_iflnk)
        {
            auto reduced = reduce(parent_res->parent, parent_dir);
            if (!reduced.has_value())
                return -lib::map_error(reduced.error());
            parent_dir = std::move(*reduced);
        }

        const auto &parent_stat = parent_dir.dentry->inode->stat;
        if (parent_stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (detail::readonly_mount(parent_dir))
            return -EROFS;

        if (!sched::check_perms(proc->cred, parent_stat, sched::access_mode::write))
            return -EACCES;

        auto created = symlink(*parent, link, std::move(*target_val));
        if (!created.has_value())
            return -lib::map_error(created.error());

        {
            const std::unique_lock _ { created->dentry->inode->lock };

            auto &stat = created->dentry->inode->stat;
            stat.st_mode |= (s_irwxu | s_irwxg | s_irwxo);
            stat.st_uid = proc->cred->euid;
            stat.st_gid = proc->cred->egid;

            if (const auto ret = dirty_inode(*created); !ret)
                return -lib::map_error(ret.error());
        }
        return 0;
    }

    int symlink(const char __user *target, const char __user *linkpath)
    {
        return symlinkat(target, at_fdcwd, linkpath);
    }

    int renameat2(
        int olddfd, const char __user *oldname, int newdfd, const char __user *newname,
        unsigned int flags
    )
    {
        // TODO: glibc seems to fall back to renameat
        lib::unused(olddfd, oldname, newdfd, newname, flags);
        return -ENOSYS;
    }

    int renameat(int olddirfd, const char __user *oldpath, int newdirfd, const char __user *newpath)
    {
        const auto proc = sched::current_process();

        auto old_val = detail::get_path(oldpath);
        if (!old_val.has_value())
            return -lib::map_error(old_val.error());
        const auto old_path = std::move(*old_val);

        auto new_val = detail::get_path(newpath);
        if (!new_val.has_value())
            return -lib::map_error(new_val.error());
        const auto new_path = std::move(*new_val);

        const auto old_anchor = detail::get_parent(proc, olddirfd, old_path);
        if (!old_anchor.has_value())
            return -lib::map_error(old_anchor.error());

        const auto new_anchor = detail::get_parent(proc, newdirfd, new_path);
        if (!new_anchor.has_value())
            return -lib::map_error(new_anchor.error());

        const auto old_parent = detail::resolve_parent_dir(proc, olddirfd, old_path.dirname());
        if (!old_parent.has_value())
            return -lib::map_error(old_parent.error());

        const auto new_parent = detail::resolve_parent_dir(proc, newdirfd, new_path.dirname());
        if (!new_parent.has_value())
            return -lib::map_error(new_parent.error());

        if (detail::readonly_mount(*old_parent) || detail::readonly_mount(*new_parent))
            return -EROFS;

        const auto &old_parent_stat = old_parent->dentry->inode->stat;
        const auto &new_parent_stat = new_parent->dentry->inode->stat;
        if (!sched::check_perms(proc->cred, old_parent_stat, sched::access_mode::write) ||
            !sched::check_perms(proc->cred, new_parent_stat, sched::access_mode::write))
            return -EACCES;

        const auto sticky_ok = [&](const path &parent_path, const path &target_path) -> bool {
            const auto &pstat = parent_path.dentry->inode->stat;
            if (!(pstat.st_mode & s_isvtx))
                return true;

            const auto &cred = proc->cred;
            const auto &tstat = target_path.dentry->inode->stat;
            return cred->fsuid == tstat.st_uid || cred->fsuid == pstat.st_uid ||
                sched::capable(cred, sched::cap_t::fowner);
        };

        if (const auto tgt = detail::resolve_from(proc, olddirfd, old_path); tgt.has_value())
        {
            if (!sticky_ok(*old_parent, tgt->target))
                return -EACCES;
        }
        if (const auto tgt = detail::resolve_from(proc, newdirfd, new_path); tgt.has_value())
        {
            if (!sticky_ok(*new_parent, tgt->target))
                return -EACCES;
        }

        if (const auto ret = rename(*old_anchor, old_path, *new_anchor, new_path); !ret)
            return -lib::map_error(ret.error());

        return 0;
    }

    int rename(const char __user *oldpath, const char __user *newpath)
    {
        return renameat(at_fdcwd, oldpath, at_fdcwd, newpath);
    }

    int utimensat(int dirfd, const char __user *pathname, const timespec __user *times, int flags)
    {
        constexpr int utime_now = ((1l << 30) - 1l);
        constexpr int utime_omit = ((1l << 30) - 2l);

        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return -EINVAL;

        if (pathname == nullptr && dirfd != at_fdcwd)
            flags |= at_empty_path;

        const auto now = chrono::now(chrono::type::realtime);

        timespec ktimes[2];
        if (times != nullptr)
        {
            if (!lib::copy_from_user(ktimes, times, sizeof(timespec) * 2))
                return -EFAULT;

            if (ktimes[0].tv_nsec == utime_omit && ktimes[1].tv_nsec == utime_omit)
                return 0;
        }
        else
            ktimes[0] = ktimes[1] = now;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (detail::readonly_mount(*target))
            return -EROFS;

        auto &inode = target->dentry->inode;
        auto &stat = inode->stat;

        const auto &cred = proc->cred;
        const bool is_owner = (cred->fsuid == stat.st_uid);
        const bool has_fowner = sched::capable(cred, sched::cap_t::fowner);

        const auto is_special = [](const auto ns) { return ns == utime_now || ns == utime_omit; };

        if (times && (!is_special(ktimes[0].tv_nsec) || !is_special(ktimes[1].tv_nsec)))
        {
            if (!is_owner && !has_fowner)
                return -EPERM;
        }
        else if (
            !is_owner && !has_fowner &&
            !vfs::check_access(
                *target, proc->cred, static_cast<std::uint32_t>(sched::access_mode::write)
            )
        )
            return -EACCES;

        for (auto &ktime : ktimes)
        {
            if (ktime.tv_nsec == utime_now)
                ktime = now;
        }

        {
            const std::unique_lock _ { inode->lock };

            if (ktimes[0].tv_nsec != utime_omit)
                stat.st_atim = ktimes[0];
            if (ktimes[1].tv_nsec != utime_omit)
                stat.st_mtim = ktimes[1];
            stat.st_ctim = now;

            if (const auto ret = dirty_inode(*target); !ret)
                return -lib::map_error(ret.error());
        }
        return 0;
    }
} // namespace syscall::vfs
