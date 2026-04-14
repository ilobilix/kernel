// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.scheduler;
import system.memory.virt;
import system.chrono;
import system.vfs;
import system.vfs.pipe;
import magic_enum;
import arch;
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

        std::optional<resolve_res> resolve_from(sched::process *proc, int dirfd, lib::path_view path)
        {
            auto parent = get_parent(proc, dirfd, path);
            if (!parent.has_value())
                return std::nullopt;

            auto res = resolve(std::move(parent), path);
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

        std::optional<path> get_target(sched::process *proc, int dirfd, const char __user *pathname, bool follow_links, bool empty_path)
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

            auto val = get_path(pathname);
            if (!val.has_value())
                return std::nullopt;

            auto res = resolve_from(proc, dirfd, std::move(*val));
            if (!res.has_value())
                return std::nullopt;

            auto target = std::move(res->target);
            lib::bug_on(!target.dentry || !target.dentry->inode);

            if (follow_links)
            {
                auto reduced = reduce(std::move(res->parent), std::move(target));
                if (!reduced.has_value())
                    return (errno = lib::map_error(reduced.error()), std::nullopt);
                target = std::move(*reduced);
            }
            return target;
        }
    } // namespace

    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode)
    {
        const auto proc = sched::this_thread()->parent;
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

            const auto cmode = (mode & ~proc->umask) | stat::type::s_ifreg;
            auto created = create(parent->target, pathstr.basename(), cmode);
            if (!created.has_value())
                return (errno = lib::map_error(created.error()), -1);

            lib::bug_on(!created->dentry || !created->dentry->inode);

            const auto &parent_stat = parent->target.dentry->inode->stat;
            auto &stat = created->dentry->inode->stat;

            stat.st_uid = proc->euid;
            if (parent_stat.mode() & s_isgid)
                stat.st_gid = parent_stat.st_gid;
            else
                stat.st_gid = proc->egid;

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
        const auto fd = fdt.allocate_fd(fdesc, 0, false);
        if (fd < 0)
            return (errno = EMFILE, -1);

        if (const auto ret = fdesc->file->open(flags); !ret)
        {
            fdt.close(fd);
            return (errno = lib::map_error(ret.error()), -1);
        }

        if ((flags & o_trunc) && write)
        {
            if (const auto ret = fdesc->file->trunc(0); !ret)
                return (errno = lib::map_error(ret.error()), -1);

            stat.update_time(stat::time::modify | stat::time::status);
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
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        if (fdesc->file->ref.fetch_sub(1) == 1)
        {
            lib::bug_on(!proc->fdt.close(fd));
            if (const auto ret = fdesc->file->close(); !ret)
                return (errno = lib::map_error(ret.error()), -1);
        }
        return 0;
    }

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->read(*uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        stat.update_time(stat::time::access);
        return *ret;
    }

    std::ssize_t write(int fd, const void __user *buf, std::size_t count)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->write(*uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        // TODO: sync

        stat.update_time(stat::time::modify | stat::time::status);
        return *ret;
    }

    std::ssize_t pread(int fd, void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), *uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        stat.update_time(stat::time::access);
        return *ret;
    }

    std::ssize_t pwrite(int fd, const void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return (errno = EBADF, -1);

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
            return (errno = EISDIR, -1);

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), *uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

        stat.update_time(stat::time::modify | stat::time::status);
        return *ret;
    }

    struct iovec
    {
        void __user *iov_base;
        std::size_t iov_len;
    };

    std::ssize_t readv(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
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

            const auto ret = fdesc->file->read(*uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            if (*ret == 0)
                break;

            total_read += *ret;
        }

        stat.update_time(stat::time::access);
        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t writev(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
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

            const auto ret = fdesc->file->write(*uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            total_written += *ret;
        }

        stat.update_time(stat::time::modify | stat::time::status);
        return static_cast<std::ssize_t>(total_written);
    }

    std::ssize_t preadv(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
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

            const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), *uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            if (*ret == 0)
                break;

            total_read += *ret;
            offset += static_cast<off_t>(*ret);
        }

        stat.update_time(stat::time::access);
        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t pwritev(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &file = fdesc->file;
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

            const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), *uspan);
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), -1);

            total_written += *ret;
            offset += static_cast<off_t>(*ret);
        }

        stat.update_time(stat::time::modify | stat::time::status);
        return static_cast<std::ssize_t>(total_written);
    }

    // std::ssize_t preadv2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);
    // std::ssize_t pwritev2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);

    off_t lseek(int fd, off_t offset, int whence)
    {
        const auto proc = sched::this_thread()->parent;

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

    int faccessat2(int dirfd, const char __user *pathname, int mode, int flags)
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

        if (mode & ~(r_ok | w_ok | x_ok))
            return (errno = EINVAL, -1);

        const auto supgids = proc->supplementary_gids.read_lock();
        if (!check_access(uid, gid, *supgids, target->dentry->inode->stat, mode))
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

    int ioctl(int fd, unsigned long request, void __user *argp)
    {
        const auto proc = sched::this_thread()->parent;

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
        const auto proc = sched::this_thread()->parent;

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

        const auto path_str = pathname_from(proc->cwd);
        if (path_str.size() + 1 > size)
            return (errno = ERANGE, nullptr);

        if (!lib::copy_to_user(buf, path_str.c_str(), path_str.size() + 1))
            return (errno = EFAULT, nullptr);
        return lib::remove_user_cast<char>(buf);
    }

    int chdir(const char __user *pathname)
    {
        const auto proc = sched::this_thread()->parent;

        const auto target = get_target(proc, at_fdcwd, pathname, true, false);
        if (!target.has_value())
            return -1;

        if (target->dentry->inode->stat.type() != stat::type::s_ifdir)
            return (errno = ENOTDIR, -1);

        proc->cwd = *target;
        return 0;
    }

    int fchdir(int fd)
    {
        const auto proc = sched::this_thread()->parent;

        const auto target = get_target(proc, fd, nullptr, true, true);
        if (!target.has_value())
            return -1;

        if (target->dentry->inode->stat.type() != stat::type::s_ifdir)
            return (errno = ENOTDIR, -1);

        proc->cwd = *target;
        return 0;
    }

    int pipe2(int __user *pipefd, int flags)
    {
        const auto proc = sched::this_thread()->parent;
        auto &fdt = proc->fdt;

        if (flags & ~(o_closexec | o_direct | o_nonblock))
            return (errno = EINVAL, -1);

        auto shared_inode = std::make_shared<inode>();
        {
            shared_inode->stat.st_blksize = 0x1000;
            shared_inode->stat.st_mode = std::to_underlying(stat::s_ififo) | s_irwxu | s_irwxg | s_irwxo;
            shared_inode->stat.st_uid = proc->euid;
            shared_inode->stat.st_gid = proc->egid;

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
        fds[0] = fdt.allocate_fd(rfdesc, 0, false);
        if (fds[0] < 0)
            return (errno = EMFILE, -1);

        if (const auto ret = rfdesc->file->open(flags | o_rdonly); !ret)
        {
            fdt.close(fds[0]);
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
            return (errno = EMFILE, -1);
        fds[1] = fdt.allocate_fd(wfdesc, 0, false);
        if (fds[1] < 0)
        {
            proc->fdt.close(fds[0]);
            return (errno = EMFILE, -1);
        }

        wfdesc->file->private_data = rfdesc->file->private_data;
        if (const auto ret = wfdesc->file->open(flags | o_wronly); !ret)
        {
            fdt.close(fds[0]);
            fdt.close(fds[1]);
            return (errno = lib::map_error(ret.error()), -1);
        }

        if (!lib::copy_to_user(pipefd, fds.data(), sizeof(int) * 2))
        {
            proc->fdt.close(fds[0]);
            proc->fdt.close(fds[1]);
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

    struct dirent64
    {
        std::uint64_t d_ino;
        std::int64_t d_off;
        unsigned short d_reclen;
        unsigned char d_type;
        char d_name[];
    };

    int getdents64(int fd, vfs::dirent64 __user *buf, std::size_t count)
    {
        const auto proc = sched::this_thread()->parent;

        const auto fdesc = get_fd(proc, fd);
        if (fdesc == nullptr)
            return -1;

        const auto &stat = fdesc->file->path.dentry->inode->stat;
        if (stat.type() != stat::type::s_ifdir)
            return (errno = ENOTDIR, -1);

        const auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = fdesc->file->getdents(*uspan);
        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), -1);

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

        struct select_poll_table : poll_table
        {
            lib::list<lib::wait_queue_entry> wait_nodes;
            sched::thread *current;

            select_poll_table(sched::thread *current) : current { current } { }
            ~select_poll_table() override { unregister_all(); }

            void queue_wait(lib::wait_queue *wq) override
            {
                wq->add(&wait_nodes.emplace_back(static_cast<sched::thread_base *>(current)));
            }

            void unregister_all()
            {
                for (auto &node : wait_nodes)
                {
                    if (node.queue)
                        node.queue->remove(&node);
                }
                wait_nodes.clear();
            }
        };

        // TODO: ugh
        int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, timespec *timeout, bool update_timeout, const sigset_t *sigmask)
        {
            // TODO
            lib::unused(sigmask);

            const auto me = sched::this_thread();
            const auto proc = me->parent;

            std::optional<std::size_t> timeout_ms { };
            if (timeout)
                timeout_ms = timeout->to_ms();

            const auto start_time = chrono::now(chrono::monotonic).to_ns();

            const auto check_files = [&](select_poll_table *table, bool write_back) -> lib::expect<int>
            {
                int events = 0;

                fd_set out_read, out_write, out_except;
                FD_ZERO(&out_read); FD_ZERO(&out_write); FD_ZERO(&out_except);

                for (int i = 0; i < nfds; i++)
                {
                    if (!(
                        (readfds ? FD_ISSET(i, readfds) : 0) |
                        (writefds ? FD_ISSET(i, writefds) : 0) |
                        (exceptfds ? FD_ISSET(i, exceptfds) : 0)
                    )) continue;

                    auto fd = proc->fdt.get(i);
                    if (!fd || !fd->file)
                        return std::unexpected { lib::err::invalid_fd };

                    const auto pres = fd->file->poll(table);
                    if (!pres.has_value())
                        continue;

                    if (readfds && FD_ISSET(i, readfds) && (*pres & pollin))
                    {
                        FD_SET(i, &out_read);
                        events++;
                    }
                    if (writefds && FD_ISSET(i, writefds) && (*pres & pollout))
                    {
                        FD_SET(i, &out_write);
                        events++;
                    }
                    if (exceptfds && FD_ISSET(i, exceptfds) && (*pres & pollpri))
                    {
                        FD_SET(i, &out_except);
                        events++;
                    }
                }

                if (write_back)
                {
                    if (events > 0)
                    {
                        if (readfds)
                            std::memcpy(readfds, &out_read, sizeof(fd_set));
                        if (writefds)
                            std::memcpy(writefds, &out_write, sizeof(fd_set));
                        if (exceptfds)
                            std::memcpy(exceptfds, &out_except, sizeof(fd_set));
                    }
                    else
                    {
                        if (readfds)
                            FD_ZERO(readfds);
                        if (writefds)
                            FD_ZERO(writefds);
                        if (exceptfds)
                            FD_ZERO(exceptfds);
                    }
                }
                return events;
            };

            select_poll_table table { me };

            auto events = check_files(&table, false);
            if (!events.has_value())
                return (errno = lib::map_error(events.error()), -1);
            if (*events > 0)
            {
                if (const auto ret = check_files(nullptr, true); !ret.has_value())
                    return (errno = lib::map_error(ret.error()), -1);
                goto exit;
            }

            while (true)
            {
                for (auto &node : table.wait_nodes)
                    node.triggered.store(false, std::memory_order_seq_cst);

                events = check_files(nullptr, false);
                if (!events.has_value())
                    return (errno = lib::map_error(events.error()), -1);

                if (*events > 0)
                {
                    if (const auto ret = check_files(nullptr, true); !ret.has_value())
                        return (errno = lib::map_error(ret.error()), -1);
                    goto exit;
                }

                if (timeout_ms.has_value())
                {
                    const auto now = chrono::now(chrono::monotonic).to_ns();
                    const auto elapsed_ms = (now - start_time) / 1'000'000;
                    if (elapsed_ms >= *timeout_ms)
                    {
                        events = 0;
                        if (const auto ret = check_files(nullptr, true); !ret.has_value())
                            return (errno = lib::map_error(ret.error()), -1);
                        goto exit;
                    }
                    me->prepare_sleep(*timeout_ms - elapsed_ms);
                }
                else me->prepare_sleep();

                bool race = false;
                for (auto &node : table.wait_nodes)
                {
                    if (node.triggered.load(std::memory_order_seq_cst))
                    {
                        race = true;
                        break;
                    }
                }

                if (race)
                {
                    me->status = sched::status::running;
                    me->sleep_lock.unlock();
                    arch::int_switch(me->sleep_ints);
                    continue;
                }
                sched::yield();
            }

            exit:
            if (update_timeout && timeout != nullptr && timeout_ms.has_value())
            {
                const auto elapsed_ns = chrono::now(chrono::monotonic).to_ns() - start_time;
                const auto orig_ns = *timeout_ms * 1'000'000;
                const auto remaining_ns = (orig_ns > elapsed_ns) ? (orig_ns - elapsed_ns) : 0;

                timeout->tv_sec = remaining_ns / 1'000'000'000;
                timeout->tv_nsec = remaining_ns % 1'000'000'000;
            }
            return *events;
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
