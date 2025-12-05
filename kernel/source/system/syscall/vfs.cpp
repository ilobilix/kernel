// Copyright (C) 2024-2025  ilobilo

module system.syscall.vfs;

import system.scheduler;
import system.memory.virt;
import system.vfs;
import magic_enum;
import lib;
import std;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        std::shared_ptr<filedesc> get_fd(sched::process *proc, int fdnum)
        {
            if (fdnum < 0)
                return (errno = EBADF, nullptr);
            return proc->fdt.get(fdnum) ?: (errno = EBADF, nullptr);
        }

        std::optional<path> get_parent(sched::process *proc, int dirfd, lib::path_view path)
        {
            if (path.is_absolute())
                return get_root(true);

            if (dirfd == at_fdcwd)
                return proc->cwd;

            auto fd = get_fd(proc, dirfd);
            if (fd == nullptr)
                return std::nullopt;

            if (fd->file->path.dentry->inode->stat.type() != stat::type::s_ifdir)
                return (errno = ENOTDIR, std::nullopt);

            return fd->file->path;
        }

        constexpr errnos map_error(error err)
        {
            switch (err)
            {
                case error::todo:
                    return ENOSYS;
                case error::already_exists:
                    return EEXIST;
                case error::not_found:
                    return ENOENT;
                case error::not_a_dir:
                    return ENOTDIR;
                case error::not_a_block:
                    return ENOTBLK;
                case error::symloop_max:
                    return ELOOP;
                case error::target_is_a_dir:
                    return EISDIR;
                case error::target_is_busy:
                    return EBUSY;
                case error::dir_not_empty:
                    return ENOTEMPTY;
                case error::different_filesystem:
                    return EXDEV;
                case error::invalid_filesystem:
                    return ENODEV;
                case error::invalid_mount:
                case error::invalid_symlink:
                    return EINVAL;
                default:
                    lib::panic("unhandled vfs error: {}", magic_enum::enum_name(err));
            }
            std::unreachable();
        }

        std::optional<resolve_res> resolve_from(sched::process *proc, int dirfd, lib::path_view path)
        {
            const auto parent = get_parent(proc, dirfd, path);
            if (!parent.has_value())
                return std::nullopt;

            const auto res = vfs::resolve(parent, path);
            if (!res)
                return (errno = map_error(res.error()), std::nullopt);

            return res.value();
        }

        std::optional<lib::path> get_path(const char __user *pathname)
        {
            if (pathname == nullptr)
                return (errno = EFAULT, std::nullopt);

            const auto pathname_len = lib::strnlen_user(pathname, vfs::path_max);
            if (pathname_len < 0)
                return (errno = EFAULT, std::nullopt);
            if (pathname_len == 0)
                return (errno = EINVAL, std::nullopt);
            if (pathname_len == vfs::path_max)
                return (errno = ENAMETOOLONG, std::nullopt);

            lib::path path { static_cast<std::size_t>(pathname_len), 0 };
            lib::bug_on(path.str().size() != static_cast<std::size_t>(pathname_len));

            if (!lib::copy_from_user(path.str().data(), pathname, pathname_len))
                return (errno = EFAULT, std::nullopt);

            path.normalise();
            return std::move(path);
        }

        std::optional<vfs::path> get_target(sched::process *proc, int dirfd, const char __user *pathname, bool follow_links, bool empty_path)
        {
            if (empty_path)
            {
                if (dirfd == at_fdcwd)
                    return proc->cwd;

                auto fd = get_fd(proc, dirfd);
                if (fd == nullptr)
                    return std::nullopt;

                return fd->file->path;
            }

            const auto val = get_path(pathname);
            if (!val.has_value())
                return std::nullopt;

            const auto path = val.value();

            vfs::path target { };
            const auto res = resolve_from(proc, dirfd, path);
            if (!res.has_value())
                return std::nullopt;

            target = res->target;
            lib::bug_on(!target.dentry || !target.dentry->inode);

            if (follow_links)
            {
                const auto reduced = vfs::reduce(res->parent, target);
                if (!reduced.has_value())
                    return (errno = map_error(reduced.error()), std::nullopt);
                target = reduced.value();
            }
            return target;
        }
    } // namespace

    int open(const char __user *pathname, int flags, mode_t mode)
    {
        return openat(at_fdcwd, pathname, flags, mode);
    }

    int creat(const char __user *pathname, mode_t mode)
    {
        return openat(at_fdcwd, pathname, o_creat | o_wronly | o_trunc, mode);
    }

    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode)
    {
        const auto proc = sched::this_thread()->parent;
        auto &fdt = proc->fdt;

        const bool follow_links = (flags & o_nofollow) == 0;
        const bool write = is_write(flags);

        if ((flags & o_tmpfile) && !write)
            return (errno = EINVAL, -1);

        if ((flags & o_directory) && (flags & o_creat))
            return (errno = EINVAL, -1);

        // ignore other bits
        mode &= (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

        const auto val = get_path(pathname);
        if (!val.has_value())
            return -1;

        const auto path = val.value();

        vfs::path target { };
        const auto res = resolve_from(proc, dirfd, path);
        if (!res.has_value())
        {
            if ((flags & o_creat) == 0)
                return -1;

            const auto parent = resolve_from(proc, dirfd, path.dirname());
            if (!parent.has_value())
                return -1;

            if (parent->target.dentry->inode->stat.type() != stat::type::s_ifdir)
                return (errno = ENOTDIR, -1);

            const auto created = vfs::create(parent->target, path.basename(), (mode & ~proc->umask));
            if (!created.has_value())
                return (errno = map_error(created.error()), -1);

            lib::bug_on(!created->dentry || created->dentry->inode);

            const auto &parent_stat = parent->target.dentry->inode->stat;
            auto &stat = created->dentry->inode->stat;

            stat.st_uid = proc->euid;
            if (parent_stat.mode() & s_isgid)
                stat.st_gid = parent_stat.st_gid;
            else
                stat.st_gid = proc->egid;

            target = created.value();
        }
        else if ((flags & o_excl) && (flags & o_creat))
        {
            errno = EEXIST;
            return -1;
        }
        else
        {
            target = res->target;
            lib::bug_on(!target.dentry || !target.dentry->inode);

            if (follow_links)
            {
                const auto reduced = vfs::reduce(res->parent, target);
                if (!reduced.has_value())
                    return (errno = map_error(reduced.error()), -1);
                target = reduced.value();
            }
        }

        auto &stat = target.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir && write)
            return (errno = EISDIR, -1);

        if (stat.type() != stat::s_ifdir && (flags & o_directory))
            return (errno = ENOTDIR, -1);

        auto fdesc = filedesc::create(target, flags, proc->pid);
        if (!fdesc)
            return (errno = EMFILE, -1);
        const auto fd = fdt.allocate_fd(fdesc, 0, false);
        if (fd < 0)
            return (errno = EMFILE, -1);

        if (!fdesc->file->open(flags))
        {
            fdt.close(fd);
            return (errno = EIO, -1);
        }

        if (flags & o_trunc)
        {
            if (!fdesc->file->trunc(0))
                return (errno = EINVAL, -1);

            stat.update_time(stat::time::modify | stat::time::status);
        }

        return fd;
    }

    int close(int fd)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        if (fdesc->file->ref.fetch_sub(1) == 1)
        {
            lib::bug_on(!proc->fdt.close(fd));
            if (!fdesc->file->close())
                return (errno = EIO, -1);
        }
        return 0;
    }

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->read(uspan.value());
        if (ret < 0)
            return (errno = -ret, -1);

        stat.update_time(stat::time::access);
        return ret;
    }

    std::ssize_t write(int fd, const void __user *buf, std::size_t count)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->write(uspan.value());
        if (ret < 0)
            return (errno = -ret, -1);

        // TODO: sync

        stat.update_time(stat::time::modify | stat::time::status);
        return ret;
    }

    std::ssize_t pread(int fd, void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), uspan.value());
        if (ret < 0)
            return (errno = -ret, -1);

        stat.update_time(stat::time::access);
        return ret;
    }

    std::ssize_t pwrite(int fd, const void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), uspan.value());
        if (ret < 0)
            return (errno = -ret, -1);

        stat.update_time(stat::time::modify | stat::time::status);
        return ret;
    }

    struct iovec
    {
        void __user *iov_base;
        std::size_t iov_len;
    };

    std::ssize_t readv(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        std::size_t total_read = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return (errno = EFAULT, -1);

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return (errno = EFAULT, -1);

            const auto ret = fdesc->file->read(uspan.value());
            if (ret < 0)
                return (errno = -ret, -1);

            if (ret == 0)
                break;

            total_read += static_cast<std::size_t>(ret);
        }

        stat.update_time(stat::time::access);
        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t writev(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        std::size_t total_written = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return (errno = EFAULT, -1);

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return (errno = EFAULT, -1);

            const auto ret = fdesc->file->write(uspan.value());
            if (ret < 0)
                return (errno = -ret, -1);

            total_written += static_cast<std::size_t>(ret);
        }

        stat.update_time(stat::time::modify | stat::time::status);
        return static_cast<std::ssize_t>(total_written);
    }

    std::ssize_t preadv(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        std::size_t total_read = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return (errno = EFAULT, -1);

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return (errno = EFAULT, -1);

            const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), uspan.value());
            if (ret < 0)
                return (errno = -ret, -1);

            if (ret == 0)
                break;

            total_read += static_cast<std::size_t>(ret);
            offset += static_cast<off_t>(ret);
        }

        stat.update_time(stat::time::access);
        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t pwritev(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        std::size_t total_written = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return (errno = EFAULT, -1);

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return (errno = EFAULT, -1);

            const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), uspan.value());
            if (ret < 0)
                return (errno = -ret, -1);

            total_written += static_cast<std::size_t>(ret);
            offset += static_cast<off_t>(ret);
        }

        stat.update_time(stat::time::modify | stat::time::status);
        return static_cast<std::ssize_t>(total_written);
    }

    // std::ssize_t preadv2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);
    // std::ssize_t pwritev2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);

    off_t lseek(int fd, off_t offset, int whence)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &file = fdesc->file;
        auto &stat = file->path.dentry->inode->stat;
        const auto type = stat.type();
        if (type == stat::s_ifsock || type == stat::s_ififo)
            return (errno = ESPIPE, -1);

        std::size_t new_offset = 0;
        switch (whence)
        {
            case seek_set:
                new_offset = offset;
                break;
            case seek_cur:
                if (file->offset + offset > std::numeric_limits<off_t>::max())
                    return (errno = EOVERFLOW, -1);
                new_offset = file->offset + offset;
                break;
            case seek_end:
            {
                const std::size_t size = stat.st_size;
                if (size + offset > std::numeric_limits<off_t>::max())
                    return (errno = EOVERFLOW, -1);
                new_offset = size + offset;
                break;
            }
            default:
                return (errno = EINVAL, -1);
        }

        if constexpr (std::is_signed_v<off_t>)
        {
            if (static_cast<off_t>(new_offset) < 0)
                return (errno = EOVERFLOW, -1);
        }

        return file->offset = new_offset;
    }

    int fstatat(int dirfd, const char __user *pathname, ::stat __user *statbuf, int flags)
    {
        const auto proc = sched::this_thread()->parent;

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path);
        if (!target.has_value())
            return -1;

        if (!lib::copy_to_user(statbuf, &target->dentry->inode->stat, sizeof(::stat)))
            return (errno = EFAULT, -1);
        return 0;
    }

    int stat(const char __user *pathname, struct stat __user *statbuf)
    {
        return fstatat(at_fdcwd, pathname, statbuf, 0);
    }

    int fstat(int fd, struct stat __user *statbuf)
    {
        return fstatat(fd, nullptr, statbuf, at_empty_path);
    }

    int lstat(const char __user *pathname, struct stat __user *statbuf)
    {
        return fstatat(at_fdcwd, pathname, statbuf, at_symlink_nofollow);
    }

    int faccessat(int dirfd, const char __user *pathname, int mode, int flags)
    {
        const auto proc = sched::this_thread()->parent;

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool eaccess = (flags & at_eaccess) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path);
        if (!target.has_value())
            return -1;

        if (mode == f_ok)
            return 0;

        const auto uid = eaccess ? proc->euid : proc->ruid;
        const auto gid = eaccess ? proc->egid : proc->rgid;

        if ((mode & (x_ok | w_ok | r_ok)) == 0)
            return (errno = EINVAL, -1);

        const auto supgids = proc->supplementary_gids.read_lock();
        if (!vfs::check_access(uid, gid, *supgids, target->dentry->inode->stat, mode))
            return (errno = EACCES, -1);

        return 0;
    }

    int access(const char __user *pathname, int mode)
    {
        return faccessat(at_fdcwd, pathname, mode, 0);
    }

    int ioctl(int fd, unsigned long request, void __user *argp)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        if (fdesc->file->path.dentry->inode->stat.type() != stat::type::s_ifchr)
            return (errno = ENOTTY, -1);

        return fdesc->file->ioctl(request, lib::uptr_or_addr { argp });
    }

    int fcntl(int fd, int cmd, std::uintptr_t arg)
    {
        const auto proc = sched::this_thread()->parent;

        auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        switch (cmd)
        {
            case 0: // F_DUPFD
                return dup(static_cast<int>(arg));
            case 1030: // F_DUPFD_CLOEXEC
            {
                const auto newfd = dup(static_cast<int>(arg));
                if (newfd < 0)
                    return -1;
                auto new_fdesc = proc->fdt.get(newfd);
                lib::bug_on(new_fdesc == nullptr);
                new_fdesc->closexec = true;
                return newfd;
            }
            case 1: // F_GETFD
                return fdesc->closexec ? o_closexec : 0;
            case 2: // F_SETFD
                fdesc->closexec = (arg & o_closexec) != 0;
                break;
            case 3: // F_GETFL
                return fdesc->file->flags;
            case 4: // F_SETFL
            {
                const auto new_flags = (static_cast<int>(arg) & changeable_status_flags);
                fdesc->file->flags = (fdesc->file->flags & ~changeable_status_flags) | new_flags;
                break;
            }
            default:
                return (errno = EINVAL, -1);
        }
        return 0;
    }

    int dup(int oldfd)
    {
        const auto proc = sched::this_thread()->parent;
        return proc->fdt.dup(oldfd, 0, false, false);
    }

    int dup2(int oldfd, int newfd)
    {
        const auto proc = sched::this_thread()->parent;
        return proc->fdt.dup(oldfd, newfd, false, true);
    }

    int dup3(int oldfd, int newfd, int flags)
    {
        if (oldfd == newfd || (flags & ~o_closexec) != 0)
            return (errno = EINVAL, -1);

        const auto proc = sched::this_thread()->parent;
        return proc->fdt.dup(oldfd, newfd, (flags & o_closexec) != 0, true);
    }

    char *getcwd(char __user *buf, std::size_t size)
    {
        const auto proc = sched::this_thread()->parent;

        const auto path_str = vfs::pathname_from(proc->cwd);
        if (path_str.size() + 1 > size)
            return (errno = ERANGE, nullptr);

        if (!lib::copy_to_user(buf, path_str.c_str(), path_str.size() + 1))
            return (errno = EFAULT, nullptr);
        return lib::remove_user_cast<char>(buf);
    }
} // namespace syscall::vfs