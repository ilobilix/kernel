// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.memory.virt;
import system.chrono;
import system.sched;
import system.vfs;
import system.vfs.pipe;
import system.vfs.dev;
import magic_enum;
import arch;
import lib;
import std;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        std::shared_ptr<filedesc> get_fd(sched::process_t *proc, int fdnum)
        {
            if (fdnum < 0)
                return (errno = EBADF, nullptr);
            return proc->fdt->get(fdnum) ?: (errno = EBADF, nullptr);
        }

        std::optional<path> get_parent(sched::process_t *proc, int dirfd, lib::path_view path)
        {
            if (path.is_absolute())
                return get_root(true);

            if (dirfd == at_fdcwd)
                return proc->vfs->cwd;

            auto fd = get_fd(proc, dirfd);
            if (fd == nullptr)
                return std::nullopt;

            if (fd->file->path.dentry->inode->stat.type() != stat::type::s_ifdir)
                return (errno = ENOTDIR, std::nullopt);

            return fd->file->path;
        }

        std::optional<resolve_res> resolve_from(
            sched::process_t *proc, int dirfd,
            lib::path_view path, bool automount = true
        )
        {
            auto parent = get_parent(proc, dirfd, path);
            if (!parent.has_value())
                return std::nullopt;

            auto res = resolve(std::move(parent), path, automount);
            if (!res.has_value())
                return (errno = lib::map_error(res.error()), std::nullopt);

            return *res;
        }

        std::optional<lib::path> get_path(const char __user *pathname)
        {
            if (pathname == nullptr)
                return (errno = EFAULT, std::nullopt);

            const auto pathname_len = lib::strnlen_user(pathname, path_max);
            if (pathname_len < 0)
                return (errno = EFAULT, std::nullopt);
            if (pathname_len == 0)
                return (errno = EINVAL, std::nullopt);
            if (pathname_len == path_max)
                return (errno = ENAMETOOLONG, std::nullopt);

            lib::path path { static_cast<std::size_t>(pathname_len), 0 };
            lib::bug_on(path.str().size() != static_cast<std::size_t>(pathname_len));

            if (!lib::copy_from_user(path.str().data(), pathname, pathname_len))
                return (errno = EFAULT, std::nullopt);

            path.normalise();
            return path;
        }

        int close_fd(sched::process_t *proc, int fd, bool was_opened = true)
        {
            if (!was_opened)
            {
                const auto fdesc = get_fd(proc, fd);
                if (fdesc == nullptr)
                    return -1;
                proc->fdt->fds.write_lock()->erase(fd);
                return 0;
            }

            if (!proc->fdt->close(fd))
                return (errno = EBADF, -1);

            return 0;
        }
    } // namespace

    std::optional<path> get_target(
        sched::process_t *proc, int dirfd, const char __user *pathname,
        bool follow_links, bool empty_path, bool automount
    )
    {
        if (empty_path)
        {
            bool use_dirfd = (pathname == nullptr);
            if (!use_dirfd)
            {
                const auto pathname_len = lib::strnlen_user(pathname, 1);
                if (pathname_len < 0)
                    return (errno = EFAULT, std::nullopt);
                use_dirfd = (pathname_len == 0);
            }

            if (use_dirfd)
            {
                if (dirfd == at_fdcwd)
                    return proc->vfs->cwd;

                auto fd = get_fd(proc, dirfd);
                if (fd == nullptr)
                    return std::nullopt;

                return fd->file->path;
            }
        }

        auto val = get_path(pathname);
        if (!val.has_value())
            return std::nullopt;

        auto res = resolve_from(proc, dirfd, std::move(*val), automount);
        if (!res.has_value())
            return std::nullopt;

        auto target = std::move(res->target);
        lib::bug_on(!target.dentry || !target.dentry->inode);

        if (follow_links)
        {
            auto reduced = reduce(std::move(res->parent), std::move(target), automount);
            if (!reduced.has_value())
                return (errno = lib::map_error(reduced.error()), std::nullopt);
            target = std::move(*reduced);
        }
        return target;
    }

    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode)
    {
        const auto proc = sched::current_process();
        auto &fdt = proc->fdt;

        const bool follow_links = (flags & o_nofollow) == 0;
        const bool write = is_write(flags);

        if (((flags & o_tmpfile) == o_tmpfile) && !write)
            return (errno = EINVAL, -1);

        if ((flags & o_directory) && (flags & o_creat))
            return (errno = EINVAL, -1);

        // ignore other bits
        mode &= (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

        auto val = get_path(pathname);
        if (!val.has_value())
            return -1;

        const auto pathstr = std::move(*val);

        path target { };
        auto res = resolve_from(proc, dirfd, pathstr);
        if (!res.has_value())
        {
            if ((flags & o_creat) == 0)
                return -1;

            const auto parent = resolve_from(proc, dirfd, pathstr.dirname());
            if (!parent.has_value())
                return -1;

            if (parent->target.dentry->inode->stat.type() != stat::type::s_ifdir)
                return (errno = ENOTDIR, -1);

            const auto cmode = (mode & ~proc->vfs->umask) | stat::type::s_ifreg;
            auto created = create(parent->target, pathstr.basename(), cmode);
            if (!created.has_value())
                return (errno = lib::map_error(created.error()), -1);

            lib::bug_on(!created->dentry || !created->dentry->inode);

            const auto &parent_stat = parent->target.dentry->inode->stat;
            auto &stat = created->dentry->inode->stat;

            {
                const std::unique_lock _ { created->dentry->inode->lock };

                stat.st_uid = proc->cred->euid;
                if (parent_stat.mode() & s_isgid)
                    stat.st_gid = parent_stat.st_gid;
                else
                    stat.st_gid = proc->cred->egid;

                if (const auto ret = dirty_inode(*created); !ret)
                    return (errno = lib::map_error(ret.error()), -1);
            }

            target = std::move(*created);
        }
        else if ((flags & o_excl) && (flags & o_creat))
        {
            errno = EEXIST;
            return -1;
        }
        else
        {
            target = std::move(res->target);
            lib::bug_on(!target.dentry || !target.dentry->inode);

            if (target.dentry->inode->stat.type() == stat::type::s_iflnk)
            {
                if (!follow_links)
                    return (errno = ELOOP, -1);

                auto reduced = reduce(res->parent, target);
                if (!reduced.has_value())
                    return (errno = lib::map_error(reduced.error()), -1);
                target = std::move(*reduced);
            }
        }

        auto &stat = target.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir && write)
            return (errno = EISDIR, -1);

        if (stat.type() != stat::s_ifdir && (flags & o_directory))
            return (errno = ENOTDIR, -1);

        const auto fdesc = filedesc::create(target, flags, proc->pid);
        if (!fdesc)
            return (errno = EMFILE, -1);
        const auto fd = fdt->alloc(fdesc, 0, false);
        if (fd < 0)
            return (errno = EMFILE, -1);

        if (const auto ret = fdesc->file->open(flags); !ret)
        {
            close_fd(proc, fd, false);
            return (errno = lib::map_error(ret.error()), -1);
        }

        if ((flags & o_trunc) && write)
        {
            if (const auto ret = fdesc->file->trunc(0); !ret)
                return (errno = lib::map_error(ret.error()), -1);

            {
                const std::unique_lock _ { target.dentry->inode->lock };
                stat.update_time(stat::time::modify | stat::time::status);

                if (const auto ret = dirty_inode(target); !ret)
                    return (errno = lib::map_error(ret.error()), -1);
            }

            if (flags & (o_sync | o_dsync))
            {
                if (const auto ret = fdesc->file->sync(); !ret)
                    return (errno = lib::map_error(ret.error()), -1);
            }
        }

        return fd;
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
        const auto proc = sched::current_process();
        return close_fd(proc, fd);
    }

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->read(*uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::access);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return *ret;
    }

    std::ssize_t write(int fd, const void __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->write(*uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::modify | stat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return *ret;
    }

    std::ssize_t pread(int fd, void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), *uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::access);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return *ret;
    }

    std::ssize_t pwrite(int fd, const void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), *uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::modify | stat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return *ret;
    }

    struct iovec
    {
        void __user *iov_base;
        std::size_t iov_len;
    };

    std::ssize_t readv(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

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

            const auto ret = fdesc->file->read(*uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            if (*ret == 0)
                break;

            total_read += *ret;
        }

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::access);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t writev(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

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

            const auto ret = fdesc->file->write(*uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            total_written += *ret;
        }

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::modify | stat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return static_cast<std::ssize_t>(total_written);
    }

    std::ssize_t preadv(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

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

            const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), *uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            if (*ret == 0)
                break;

            total_read += *ret;
            offset += static_cast<off_t>(*ret);
        }

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::access);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t pwritev(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

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

            const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), *uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            total_written += *ret;
            offset += static_cast<off_t>(*ret);
        }

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::modify | stat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return static_cast<std::ssize_t>(total_written);
    }

    // std::ssize_t preadv2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);
    // std::ssize_t pwritev2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);

    off_t lseek(int fd, off_t offset, int whence)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        const auto &stat = file->path.dentry->inode->stat;
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
        const auto proc = sched::current_process();

        if (flags & ~(at_symlink_nofollow | at_no_automount | at_empty_path))
            return (errno = EINVAL, -1);

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool automount = true; // (flags & at_no_automount) == 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, automount);
        if (!target.has_value())
            return -1;

        if (!lib::copy_to_user(statbuf, &target->dentry->inode->stat, sizeof(::stat)))
            return (errno = EFAULT, -1);
        return 0;
    }

    int stat(const char __user *pathname, struct stat __user *statbuf)
    {
        return fstatat(at_fdcwd, pathname, statbuf, at_no_automount);
    }

    int fstat(int fd, struct stat __user *statbuf)
    {
        return fstatat(fd, nullptr, statbuf, at_empty_path | at_no_automount);
    }

    int lstat(const char __user *pathname, struct stat __user *statbuf)
    {
        return fstatat(at_fdcwd, pathname, statbuf, at_symlink_nofollow | at_no_automount);
    }

    int statx(int dirfd, const char __user *pathname, int flags, unsigned int mask, struct statx __user *statxbuf)
    {
        const auto proc = sched::current_process();

        constexpr auto valid_flags =
            at_symlink_nofollow |
            at_no_automount |
            at_empty_path |
            at_statx_sync_type;

        if ((flags & ~valid_flags) != 0)
            return (errno = EINVAL, -1);

        const auto sync_mode = flags & at_statx_sync_type;
        if (sync_mode == (at_statx_force_sync | at_statx_dont_sync))
            return (errno = EINVAL, -1);

        constexpr std::uint32_t statx_basic_stats = 0x000007FFu;
        constexpr std::uint32_t statx_mnt_id = 0x00001000u;
        constexpr std::uint32_t statx_reserved = 0x80000000u;

        if (mask & statx_reserved)
            return (errno = EINVAL, -1);

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool automount = (flags & at_no_automount) == 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, automount);
        if (!target.has_value())
            return -1;

        struct stat val;
        {
            const std::unique_lock _ { target->dentry->inode->lock };
            val = target->dentry->inode->stat;
        }

        constexpr auto to_statx_timestamp = [](const timespec &ts)
        {
            return statx_timestamp {
                .tv_sec = static_cast<std::int64_t>(ts.tv_sec),
                .tv_nsec = static_cast<std::uint32_t>(ts.tv_nsec),
                .__reserved = 0
            };
        };

        struct statx ret { };
        ret.stx_mask = statx_basic_stats;
        ret.stx_blksize = static_cast<std::uint32_t>(val.st_blksize);
        ret.stx_nlink = static_cast<std::uint32_t>(val.st_nlink);
        ret.stx_uid = static_cast<std::uint32_t>(val.st_uid);
        ret.stx_gid = static_cast<std::uint32_t>(val.st_gid);
        ret.stx_mode = static_cast<std::uint16_t>(val.st_mode);
        ret.stx_ino = static_cast<std::uint64_t>(val.st_ino);
        ret.stx_size = static_cast<std::uint64_t>(val.st_size);
        ret.stx_blocks = static_cast<std::uint64_t>(val.st_blocks);
        ret.stx_atime = to_statx_timestamp(val.st_atim);
        ret.stx_ctime = to_statx_timestamp(val.st_ctim);
        ret.stx_mtime = to_statx_timestamp(val.st_mtim);
        ret.stx_rdev_major = dev::major(val.st_rdev);
        ret.stx_rdev_minor = dev::minor(val.st_rdev);
        ret.stx_dev_major = dev::major(val.st_dev);
        ret.stx_dev_minor = dev::minor(val.st_dev);

        if ((mask & statx_mnt_id) != 0 && target->mnt != nullptr)
        {
            ret.stx_mask |= statx_mnt_id;
            ret.stx_mnt_id = target->mnt->fs.lock()->dev_id;
        }

        if (!lib::copy_to_user(statxbuf, &ret, sizeof(struct statx)))
            return (errno = EFAULT, -1);

        return 0;
    }

    int faccessat2(int dirfd, const char __user *pathname, int mode, int flags)
    {
        if ((mode & ~(f_ok | x_ok | w_ok | r_ok)) != 0)
            return (errno = EINVAL, -1);

        if ((flags & ~(at_symlink_nofollow | at_empty_path | at_eaccess)) != 0)
            return (errno = EINVAL, -1);

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool eaccess = (flags & at_eaccess) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -1;

        if (mode == f_ok)
            return 0;

        using namespace magic_enum::bitwise_operators;
        auto desired = sched::access_mode::none;
        if (mode & r_ok)
            desired |= sched::access_mode::read;
        if (mode & w_ok)
            desired |= sched::access_mode::write;
        if (mode & x_ok)
            desired |= sched::access_mode::exec;

        std::shared_ptr<sched::cred_t> cred;
        if (eaccess == false)
        {
            cred = proc->cred->clone();
            cred->fsuid = cred->ruid;
            cred->fsgid = cred->rgid;

            if (cred->ruid != 0)
                cred->effective &= ~(sched::cap_t::dac_override | sched::cap_t::dac_override);
        }
        else cred = proc->cred;

        const auto &stat = target->dentry->inode->stat;
        if (!sched::check_perms(cred, stat, desired))
            return (errno = EACCES, -1);

        return 0;
    }

    int faccessat(int dirfd, const char __user *pathname, int mode)
    {
        return faccessat2(dirfd, pathname, mode, 0);
    }

    int access(const char __user *pathname, int mode)
    {
        return faccessat2(at_fdcwd, pathname, mode, 0);
    }

    int fchmodat(int dirfd, const char __user *pathname, mode_t mode, int flags)
    {
        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return (errno = EINVAL, -1);

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -1;

        auto &inode = target->dentry->inode;
        {
            const std::unique_lock _ { inode->lock };

            constexpr auto bits = (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

            inode->stat.st_mode = (inode->stat.st_mode & ~bits) | (mode & bits);
            inode->stat.update_time(stat::time::status);

            if (const auto ret = dirty_inode(*target); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }
        return 0;
    }

    int chmod(const char __user *pathname, mode_t mode)
    {
        return fchmodat(at_fdcwd, pathname, mode, 0);
    }

    int fchmod(int fd, mode_t mode)
    {
        return fchmodat(fd, nullptr, mode, at_empty_path);
    }

    int fchownat(int dirfd, const char __user *pathname, uid_t owner, gid_t group, int flags)
    {
        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return (errno = EINVAL, -1);

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -1;

        auto &inode = target->dentry->inode;
        {
            const std::unique_lock _ { inode->lock };

            const bool change_uid = (owner != static_cast<uid_t>(-1));
            const bool change_gid = (group != static_cast<gid_t>(-1));

            if (change_uid || change_gid)
            {
                const auto &cred = proc->cred;

                const bool uid_noop = !change_uid || (owner == inode->stat.st_uid);
                const bool gid_noop = !change_gid || (group == inode->stat.st_gid);

                const bool gid_allowed = gid_noop ||
                    (cred->fsgid == group || cred->supp_gids.contains(group));

                const bool is_owner = (cred->fsuid == inode->stat.st_uid);
                const bool allow_unpriv = is_owner && uid_noop && gid_allowed;

                if (!allow_unpriv && !sched::capable(cred, sched::cap_t::chown))
                    return (errno = EPERM, -1);

                const auto old_uid = inode->stat.st_uid;
                const auto old_gid = inode->stat.st_gid;
                const auto old_mode = inode->stat.st_mode;

                if (change_uid && !uid_noop && inode->xattrs.contains(xattr_caps_name))
                {
                    lib::bug_on(!target->mnt);

                    auto fs = target->mnt->fs.lock();
                    if (!fs.get())
                        return (errno = EIO, -1);

                    if (const auto ret = fs->remxattr(inode, xattr_caps_name); !ret)
                        return (errno = lib::map_error(ret.error()), -1);

                    inode->xattrs.erase(xattr_caps_name);
                }

                if (change_uid)
                    inode->stat.st_uid = owner;
                if (change_gid)
                    inode->stat.st_gid = group;

                if (!sched::capable(cred, sched::cap_t::fsetid))
                {
                    inode->stat.st_mode &= ~s_isuid;
                    if (inode->stat.st_mode & s_ixgrp)
                        inode->stat.st_mode &= ~s_isgid;
                }

                inode->stat.update_time(stat::time::status);
                if (const auto ret = dirty_inode(*target); !ret)
                {
                    // TODO: restore xattr
                    inode->stat.st_uid = old_uid;
                    inode->stat.st_gid = old_gid;
                    inode->stat.st_mode = old_mode;
                    return (errno = lib::map_error(ret.error()), -1);
                }
            }
        }
        return 0;
    }

    int chown(const char __user *pathname, uid_t owner, gid_t group)
    {
        return fchownat(at_fdcwd, pathname, owner, group, 0);
    }

    int fchown(int fd, uid_t owner, gid_t group)
    {
        return fchownat(fd, nullptr, owner, group, at_empty_path);
    }

    int lchown(const char __user *pathname, uid_t owner, gid_t group)
    {
        return fchownat(at_fdcwd, pathname, owner, group, at_symlink_nofollow);
    }

    std::ssize_t readlinkat(int dirfd, const char __user *pathname, char __user *buf, std::size_t bufsiz)
    {
        const auto proc = sched::current_process();

        const auto target = get_target(proc, dirfd, pathname, false, true, true);
        if (!target.has_value())
            return -1;

        if (target->dentry->inode->stat.type() != stat::type::s_iflnk)
            return (errno = EINVAL, -1);

        const auto link = target->dentry->symlinked_to;
        const auto to_copy = static_cast<std::size_t>(std::min<std::size_t>(bufsiz, link.size()));

        if (to_copy > 0)
        {
            if (!lib::copy_to_user(buf, link.data(), to_copy))
                return (errno = EFAULT, -1);
        }

        return static_cast<std::ssize_t>(to_copy);
    }

    std::ssize_t readlink(const char __user *pathname, char __user *buf, std::size_t bufsiz)
    {
        return readlinkat(at_fdcwd, pathname, buf, bufsiz);
    }

    int ioctl(int fd, unsigned long request, void __user *argp)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto ret = fdesc->file->ioctl(request, lib::uptr_or_addr { argp });
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);
        return *ret;
    }

    int fcntl(int fd, int cmd, std::uintptr_t arg)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
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
                auto new_fdesc = proc->fdt->get(newfd);
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
        const auto proc = sched::current_process();
        return proc->fdt->dup(oldfd, 0, false, false);
    }

    int dup2(int oldfd, int newfd)
    {
        const auto proc = sched::current_process();
        return proc->fdt->dup(oldfd, newfd, false, true);
    }

    int dup3(int oldfd, int newfd, int flags)
    {
        if (oldfd == newfd || (flags & ~o_closexec) != 0)
            return (errno = EINVAL, -1);

        const auto proc = sched::current_process();
        return proc->fdt->dup(oldfd, newfd, (flags & o_closexec) != 0, true);
    }

    char *getcwd(char __user *buf, std::size_t size)
    {
        const auto proc = sched::current_process();

        const auto path_str = pathname_from(proc->vfs->cwd);
        if (path_str.size() + 1 > size)
            return (errno = ERANGE, nullptr);

        if (!lib::copy_to_user(buf, path_str.c_str(), path_str.size() + 1))
            return (errno = EFAULT, nullptr);
        return lib::remove_user_cast<char>(buf);
    }

    int chdir(const char __user *pathname)
    {
        const auto proc = sched::current_process();

        const auto target = get_target(proc, at_fdcwd, pathname, true, false, true);
        if (!target.has_value())
            return -1;

        if (target->dentry->inode->stat.type() != stat::type::s_ifdir)
            return (errno = ENOTDIR, -1);

        proc->vfs->cwd = *target;
        return 0;
    }

    int fchdir(int fd)
    {
        const auto proc = sched::current_process();

        const auto target = get_target(proc, fd, nullptr, true, true, true);
        if (!target.has_value())
            return -1;

        if (target->dentry->inode->stat.type() != stat::type::s_ifdir)
            return (errno = ENOTDIR, -1);

        proc->vfs->cwd = *target;
        return 0;
    }

    int pipe2(int __user *pipefd, int flags)
    {
        const auto proc = sched::current_process();
        auto &fdt = proc->fdt;

        if (flags & ~(o_closexec | o_direct | o_nonblock))
            return (errno = EINVAL, -1);

        auto shared_inode = std::make_shared<inode>();
        {
            shared_inode->stat.st_blksize = 0x1000;
            shared_inode->stat.st_mode = std::to_underlying(stat::s_ififo) | s_irwxu | s_irwxg | s_irwxo;
            shared_inode->stat.st_uid = proc->cred->euid;
            shared_inode->stat.st_gid = proc->cred->egid;

            shared_inode->stat.update_time(
                stat::time::access |
                stat::time::modify |
                stat::time::status
            );
        }

        std::array<int, 2> fds;

        const auto rdentry = std::make_shared<dentry>();
        rdentry->name = "<[PIPE READ]>";
        rdentry->inode = shared_inode;

        const auto rfdesc = filedesc::create({
            .dentry = rdentry,
            .mnt = nullptr
        }, flags | o_rdonly, proc->pid);

        if (!rfdesc)
            return (errno = EMFILE, -1);
        rfdesc->closexec = (flags & o_closexec) != 0;

        fds[0] = fdt->alloc(rfdesc, 0, false);
        if (fds[0] < 0)
            return (errno = EMFILE, -1);

        if (const auto ret = rfdesc->file->open(flags | o_rdonly); !ret)
        {
            close_fd(proc, fds[0], false);
            return (errno = lib::map_error(ret.error()), -1);
        }

        const auto wdentry = std::make_shared<dentry>();
        wdentry->name = "<[PIPE WRITE]>";
        wdentry->inode = std::move(shared_inode);

        const auto wfdesc = filedesc::create({
            .dentry = wdentry,
            .mnt = nullptr
        }, flags | o_wronly, proc->pid);

        if (!wfdesc)
        {
            close_fd(proc, fds[0]);
            return (errno = EMFILE, -1);
        }
        wfdesc->closexec = (flags & o_closexec) != 0;

        fds[1] = fdt->alloc(wfdesc, 0, false);
        if (fds[1] < 0)
        {
            close_fd(proc, fds[0]);
            return (errno = EMFILE, -1);
        }

        wfdesc->file->private_data = rfdesc->file->private_data;
        if (const auto ret = wfdesc->file->open(flags | o_wronly); !ret)
        {
            close_fd(proc, fds[0]);
            close_fd(proc, fds[1], false);
            return (errno = lib::map_error(ret.error()), -1);
        }

        if (!lib::copy_to_user(pipefd, fds.data(), sizeof(int) * 2))
        {
            close_fd(proc, fds[0]);
            close_fd(proc, fds[1]);
            return (errno = EFAULT, -1);
        }
        return 0;
    }

    int pipe(int __user *pipefd)
    {
        return pipe2(pipefd, 0);
    }

    int socket(int domain, int type, int protocol)
    {
        lib::unused(domain, type, protocol);
        return (errno = ENOSYS, -1);
    }

    int getdents64(int fd, dirent64 __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        auto &dentry = fdesc->file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() != stat::type::s_ifdir)
            return (errno = ENOTDIR, -1);

        const auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->getdents(*uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(stat::time::access);

            if (const auto ret = dirty_inode(fdesc->file->path); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }

        return static_cast<int>(ret.value());
    }

    constexpr int FD_SETSIZE = 1024;
    struct [[aligned(alignof(long))]] fd_set
    {
        std::uint8_t fds_bits[FD_SETSIZE / 8];
    };
    static_assert(sizeof(fd_set) == FD_SETSIZE / 8);

    struct sigset_t
    {
        unsigned long sig[1024 / (8 * sizeof(long))];
    };

    namespace
    {
        // inline void FD_CLR(int fd, fd_set *set)
        // {
        //     lib::bug_on(fd >= FD_SETSIZE);
        //     set->fds_bits[fd / 8] &= ~(1 << (fd % 8));
        // }

        inline int FD_ISSET(int fd, const fd_set *set)
        {
            lib::bug_on(fd >= FD_SETSIZE);
            return set->fds_bits[fd / 8] & (1 << (fd % 8));
        }

        inline void FD_SET(int fd, fd_set *set) {
            lib::bug_on(fd >= FD_SETSIZE);
            set->fds_bits[fd / 8] |= 1 << (fd % 8);
        }

        inline void FD_ZERO(fd_set *set)
        {
            std::memset(set->fds_bits, 0, sizeof(fd_set));
        }

        struct poll_table : ::vfs::poll_table
        {
            struct entry
            {
                sched::wait_queue_entry_t entry;
                sched::wait_queue_t *wq;
            };

            lib::list<entry> entries;

            poll_table() = default;
            ~poll_table()
            {
                for (auto &entry : entries)
                    entry.wq->remove_entry(entry.entry);
            }

            void add(sched::wait_queue_t &wq) override
            {
                auto &entry = entries.emplace_back();
                entry.wq = &wq;
                wq.add_entry(entry.entry);
            }
        };

        int ppoll(std::vector<pollfd> &fds, timespec *timeout, const sigset_t *sigmask)
        {
            std::uint64_t timeout_ns = 0;
            if (timeout)
            {
                if (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1'000'000'000l)
                    return (errno = EINVAL, -1);

                timeout_ns = timeout->to_ns();
            }

            auto thread = sched::current_thread();
            auto process = thread->proc;

            // TODO
            lib::unused(sigmask);
            // sigset_t old_sigmask;
            // if (sigmask)
            //     sigprocmask(SIG_SETMASK, sigmask, &old_sigmask);

            auto cleanup = [&] {
                // TODO
                // if (sigmask)
                //     restore old sigmask
            };

            struct fd_slot
            {
                std::shared_ptr<file> file;
                std::uint16_t events;
            };

            std::vector<fd_slot> slots(fds.size());

            for (nfds_t i = 0; i < fds.size(); i++)
            {
                fds[i].revents = 0;
                if (fds[i].fd < 0)
                {
                    slots[i].file = nullptr;
                    continue;
                }

                auto desc = process->fdt->get(fds[i].fd);
                if (!desc)
                {
                    fds[i].revents = pollnval;
                    slots[i].file = nullptr;
                    continue;
                }

                slots[i].file = desc->file;
                slots[i].events = fds[i].events | pollerr | pollhup;
            }

            while (true)
            {
                poll_table pt;
                std::size_t ready = 0;

                for (nfds_t i = 0; i < fds.size(); i++)
                {
                    if (!slots[i].file)
                        continue;

                    const auto ops_res = slots[i].file->get_ops();
                    if (!ops_res)
                    {
                        fds[i].revents = pollerr;
                        ready++;
                        continue;
                    }

                    auto res = ops_res->get()->poll(slots[i].file, &pt);
                    if (res.has_value())
                    {
                        fds[i].revents = *res & slots[i].events;
                        if (fds[i].revents)
                            ready++;
                    }
                }

                if (ready > 0)
                {
                    cleanup();
                    return ready;
                }

                if (timeout && timeout_ns == 0)
                {
                    cleanup();
                    return 0;
                }

                thread->state = sched::thread_state::sleeping;
                sched::sleep_entry_t sleep_timeout {
                    .thread = thread,
                    .deadline_ns = 0,
                    .expired = false,
                    .hook = { }
                };

                if (timeout)
                    sched::arm_thread_timeout(&sleep_timeout, timeout_ns);

                const bool interrupted = sched::yield();
                if (timeout)
                {
                    if (sleep_timeout.expired)
                    {
                        cleanup();
                        return 0;
                    }
                    else sched::cancel_thread_timeout(&sleep_timeout);
                }

                if (interrupted)
                {
                    cleanup();
                    return (errno = EINTR, -1);
                }
            }
        }

        int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, timespec *timeout, bool update_timeout, const sigset_t *sigmask)
        {
            if (nfds < 0 || nfds > FD_SETSIZE)
                return (errno = EINVAL, -1);

            std::vector<pollfd> pfds;
            pfds.reserve(nfds);
            for (int fd = 0; fd < nfds; fd++)
            {
                short events = 0;
                if (readfds && FD_ISSET(fd, readfds))
                    events |= pollin | pollrdhup;
                if (writefds && FD_ISSET(fd, writefds))
                    events |= pollout;
                if (exceptfds && FD_ISSET(fd, exceptfds))
                    events |= pollpri;
                if (!events)
                    continue;

                pfds.emplace_back(fd, events, 0);
            }

            const auto timer = chrono::main_timer();

            std::uint64_t start_ns = 0;
            if (update_timeout && timeout)
                start_ns = timer->ns();

            const auto ret = ppoll(pfds, timeout, sigmask);

            if (update_timeout && timeout && ret >= 0)
            {
                const auto elapsed = timer->ns() - start_ns;
                const auto requested = timeout->to_ns();

                if (elapsed >= requested)
                {
                    timeout->tv_sec = 0;
                    timeout->tv_nsec = 0;
                }
                else
                {
                    const auto left = requested - elapsed;
                    timeout->tv_sec = static_cast<time_t>(left / 1'000'000'000ul);
                    timeout->tv_nsec = static_cast<long>(left % 1'000'000'000ul);
                }
            }

            if (ret < 0)
                return ret;

            if (readfds)
                FD_ZERO(readfds);
            if (writefds)
                FD_ZERO(writefds);
            if (exceptfds)
                FD_ZERO(exceptfds);

            int ready = 0;
            for (auto &pfd : pfds)
            {
                bool any = false;
                if (readfds && (pfd.revents & (pollin  | pollhup | pollrdhup)))
                {
                    FD_SET(pfd.fd, readfds);
                    any = true;
                }
                if (writefds && (pfd.revents & (pollout | pollhup)))
                {
                    FD_SET(pfd.fd, writefds);
                    any = true;
                }
                if (exceptfds && (pfd.revents & (pollpri | pollerr)))
                {
                    FD_SET(pfd.fd, exceptfds);
                    any = true;
                }
                if (any)
                    ready++;
            }
            return ready;
        }

        int pselect(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, timespec *timeout, bool update_timeout, const sigset_t __user *sigmask)
        {
            fd_set kreadfds, kwritefds, kexceptfds;
            sigset_t ksigmask;

            if (readfds && !lib::copy_from_user(&kreadfds, readfds, sizeof(fd_set)))
                    return (errno = EFAULT, -1);
            if (writefds && !lib::copy_from_user(&kwritefds, writefds, sizeof(fd_set)))
                    return (errno = EFAULT, -1);
            if (exceptfds && !lib::copy_from_user(&kexceptfds, exceptfds, sizeof(fd_set)))
                    return (errno = EFAULT, -1);
            if (sigmask && !lib::copy_from_user(&ksigmask, sigmask, sizeof(sigset_t)))
                    return (errno = EFAULT, -1);

            const auto ret = pselect(nfds,
                readfds ? &kreadfds : nullptr,
                writefds ? &kwritefds : nullptr,
                exceptfds ? &kexceptfds : nullptr,
                timeout, update_timeout,
                sigmask ? &ksigmask : nullptr
            );

            if (ret >= 0)
            {
                if (readfds && !lib::copy_to_user(readfds, &kreadfds, sizeof(fd_set)))
                    return (errno = EFAULT, -1);
                if (writefds && !lib::copy_to_user(writefds, &kwritefds, sizeof(fd_set)))
                        return (errno = EFAULT, -1);
                if (exceptfds && !lib::copy_to_user(exceptfds, &kexceptfds, sizeof(fd_set)))
                        return (errno = EFAULT, -1);
            }

            return ret;
        }
    } // namespace

    int ppoll(pollfd __user *fds, nfds_t nfds, timespec __user *timeout, sigset_t __user *sigmask)
    {
        if (fds == nullptr)
            return (errno = EFAULT, -1);

        std::vector<pollfd> kfds;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto &kfd = kfds.emplace_back();
            if (!lib::copy_from_user(&kfd, &fds[i], sizeof(pollfd)))
                return (errno = EFAULT, -1);
        }

        timespec ktimeout;
        if (timeout != nullptr)
        {
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return (errno = EFAULT, -1);
        }

        sigset_t ksigmask;
        if (sigmask != nullptr)
        {
            if (!lib::copy_from_user(&ksigmask, sigmask, sizeof(sigset_t)))
                return (errno = EFAULT, -1);
        }

        const auto ret = ppoll(
            kfds,
            timeout ? &ktimeout : nullptr,
            sigmask ? &ksigmask : nullptr
        );

        for (nfds_t i = 0; i < nfds; i++)
        {
            if (!lib::copy_to_user(&fds[i], &kfds[i], sizeof(pollfd)))
                return (errno = EFAULT, -1);
        }

        if (ret >= 0 && timeout != nullptr)
        {
            if (!lib::copy_to_user(timeout, &ktimeout, sizeof(timespec)))
                return (errno = EFAULT, -1);
        }

        return ret;
    }

    int poll(pollfd __user *fds, nfds_t nfds, int timeout)
    {
        if (fds == nullptr)
            return (errno = EFAULT, -1);

        std::vector<pollfd> kfds;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto &kfd = kfds.emplace_back();
            if (!lib::copy_from_user(&kfd, &fds[i], sizeof(pollfd)))
                return (errno = EFAULT, -1);
        }

        int ret;
        if (timeout >= 0)
        {
            timespec ts {
                timeout / 1000,
                (timeout % 1000) * 1'000'000L
            };
            ret = ppoll(kfds, &ts, nullptr);
        }
        else ret = ppoll(kfds, nullptr, nullptr);

        for (nfds_t i = 0; i < nfds; i++)
        {
            if (!lib::copy_to_user(&fds[i], &kfds[i], sizeof(pollfd)))
                return (errno = EFAULT, -1);
        }

        return ret;
    }

    int select(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, timeval __user *timeout)
    {
        timespec ktimeout;
        if (timeout != nullptr)
        {
            timeval ktimeval;
            if (!lib::copy_from_user(&ktimeval, timeout, sizeof(timeval)))
                return (errno = EFAULT, -1);
            ktimeout = timespec::from_timeval(ktimeval);
        }

        const auto ret = pselect(
            nfds, readfds, writefds, exceptfds,
            timeout ? &ktimeout : nullptr,
            (timeout != nullptr), nullptr
        );

        if (ret >= 0 && timeout != nullptr)
        {
            const auto ktimeval = ktimeout.to_timeval();
            if (!lib::copy_to_user(timeout, &ktimeval, sizeof(timeval)))
                return (errno = EFAULT, -1);
        }

        return ret;
    }

    int pselect(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, const timespec __user *timeout, const sigset_t __user *sigmask)
    {
        timespec ktimeout;
        if (timeout != nullptr)
        {
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return (errno = EFAULT, -1);
        }

        return pselect(
            nfds, readfds, writefds, exceptfds,
            timeout ? &ktimeout : nullptr,
            false, sigmask
        );
    }
} // namespace syscall::vfs
