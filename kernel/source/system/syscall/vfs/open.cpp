// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import magic_enum;

namespace syscall::vfs
{
    using namespace ::vfs;
    using namespace magic_enum::bitwise_operators;

    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode)
    {
        const auto proc = sched::current_process();
        auto &fdt = proc->fdt;

        const bool follow_links = (flags & o_nofollow) == 0;
        const bool write = is_write(flags);
        const bool trunc = (flags & o_trunc) && !(flags & o_path);

        if (((flags & o_tmpfile) == o_tmpfile) && !write)
            return -EINVAL;

        if ((flags & o_directory) && (flags & o_creat))
            return -EINVAL;

        // ignore other bits
        mode &= (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

        auto val = detail::get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto pathstr = std::move(*val);

        bool did_create = false;

        path_t target { };
        auto res = detail::resolve_from(proc, dirfd, pathstr);
        if (!res.has_value())
        {
            if ((flags & o_creat) == 0)
                return -lib::map_error(res.error());

            const auto parent = detail::resolve_parent_dir(proc, dirfd, pathstr.dirname());
            if (!parent.has_value())
                return -lib::map_error(parent.error());

            const auto &parent_stat = parent->dentry->inode->stat;
            if (parent_stat.type() != stat::type::s_ifdir)
                return -ENOTDIR;

            if (detail::readonly_mount(*parent))
                return -EROFS;

            if (!sched::check_perms(proc->cred, parent_stat, sched::access_mode::write))
                return -EACCES;

            const auto cmode = (mode & ~proc->vfs->umask) | stat::type::s_ifreg;
            auto created = create(*parent, pathstr.basename(), cmode);
            if (!created.has_value())
                return -lib::map_error(created.error());

            lib::bug_on(!created->dentry || !created->dentry->inode);

            auto &stat = created->dentry->inode->stat;

            {
                const std::unique_lock _ { created->dentry->inode->lock };

                stat.st_uid = proc->cred->euid;
                if (parent_stat.mode() & s_isgid)
                    stat.st_gid = parent_stat.st_gid;
                else
                    stat.st_gid = proc->cred->egid;

                if (const auto ret = dirty_inode(*created); !ret)
                    return -lib::map_error(ret.error());
            }

            target = std::move(*created);
            did_create = true;
        }
        else if ((flags & o_excl) && (flags & o_creat))
        {
            return -EEXIST;
        }
        else
        {
            target = std::move(res->target);
            lib::bug_on(!target.dentry || !target.dentry->inode);

            if (target.dentry->inode->stat.type() == stat::type::s_iflnk)
            {
                if (!follow_links)
                    return -ELOOP;

                auto reduced = reduce(res->parent, target);
                if (!reduced.has_value())
                    return -lib::map_error(reduced.error());
                target = std::move(*reduced);
            }
        }

        auto &stat = target.dentry->inode->stat;
        const auto mflags = detail::mount_flags(target);
        const bool is_tmpfile = (flags & o_tmpfile) == o_tmpfile;
        const bool needs_write = is_tmpfile || ((write || trunc) && !did_create);

        if (needs_write && (mflags & ms_rdonly))
            return -EROFS;

        if (stat.type() == stat::s_ifdir && (write || trunc) && !is_tmpfile)
            return -EISDIR;

        if (stat.type() != stat::s_ifdir && (flags & o_directory))
            return -ENOTDIR;

        if ((mflags & ms_nodev) &&
            (stat.type() == stat::s_ifchr || stat.type() == stat::s_ifblk))
            return -EACCES;

        if ((flags & o_noatime) && proc->cred->fsuid != stat.st_uid &&
            !sched::capable(proc->cred, sched::cap_t::fowner))
            return -EPERM;

        if (!(flags & o_path) && !did_create)
        {
            auto acc = sched::access_mode::none;
            if (is_read(flags))
                acc |= sched::access_mode::read;
            if (is_write(flags) || trunc)
                acc |= sched::access_mode::write;
            if (!vfs::check_access(target, proc->cred, static_cast<std::uint32_t>(acc)))
                return -EACCES;
        }

        const auto fdesc = filedesc::create(target, flags);

        const auto fdres = fdt->alloc(fdesc, 0, false, proc->rlimits->get(sched::rlimit_nofile).cur);
        if (!fdres.has_value())
            return -lib::map_error(fdres.error());

        if (const auto ret = fdesc->file->open(flags, proc->pid); !ret)
        {
            proc->fdt->close(*fdres);
            return -lib::map_error(ret.error());
        }

        if (trunc && fdesc->file->truncable())
        {
            if (const auto ret = fdesc->file->trunc(0); !ret)
                return -lib::map_error(ret.error());

            {
                const std::unique_lock _ { target.dentry->inode->lock };
                stat.update_time(kstat::time::modify | kstat::time::status);

                if (const auto ret = dirty_inode(target); !ret)
                    return -lib::map_error(ret.error());
            }

            if (flags & (o_sync | o_dsync))
            {
                if (const auto ret = fdesc->file->sync(); !ret)
                    return -lib::map_error(ret.error());
            }
        }

        return *fdres;
    }

    int open(const char __user *pathname, int flags, mode_t mode)
    {
        return openat(at_fdcwd, pathname, flags, mode);
    }

    int creat(const char __user *pathname, mode_t mode)
    {
        return openat(at_fdcwd, pathname, o_creat | o_wronly | o_trunc, mode);
    }

    int close(int fd)
    {
        return sched::current_process()->fdt->close(fd) ? 0 : -EBADF;
    }

    int close_range(std::uint32_t first, std::uint32_t last, std::uint32_t flags)
    {
        constexpr std::uint32_t unshare = 1 << 1;
        constexpr std::uint32_t cloexec = 1 << 2;

        if (first > last)
            return -EINVAL;

        if (flags & ~(unshare | cloexec))
            return -EINVAL;

        auto proc = sched::current_process();
        if (flags & unshare)
            proc->fdt = proc->fdt->clone();

        auto &fdt = proc->fdt;

        if (flags & cloexec)
        {
            for (const auto &[fd, fdesc] : *fdt->fds.read_lock())
            {
                if (fd >= static_cast<int>(first) && fd <= static_cast<int>(last))
                    fdesc->closexec.store(true, std::memory_order_relaxed);
            }
            return 0;
        }

        auto wlocked = fdt->fds.write_lock();
        for (auto it = wlocked->begin(); it != wlocked->end(); )
        {
            if (it->first >= static_cast<int>(first) && it->first <= static_cast<int>(last))
            {
                const auto closed_fd = it->first;
                it = wlocked->erase(it);
                if (closed_fd < fdt->next_fd)
                    fdt->next_fd = closed_fd;
            }
            else it++;
        }
        return 0;
    }
} // namespace syscall::vfs
