// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>
#include <user.h>

module system.syscall.vfs;

import system.scheduler;
import system.vfs.file;
import system.vfs;
import magic_enum;
import lib;
import cppstd;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        std::optional<path> get_parent(sched::process *proc, int dirfd, lib::path_view path)
        {
            if (path.is_absolute())
                return get_root(true);

            if (dirfd == at_fdcwd)
                return proc->cwd;

            if (dirfd < 0)
            {
                errno = EINVAL;
                return std::nullopt;
            }

            auto fd = proc->fdt.get(static_cast<std::size_t>(dirfd));
            if (fd == nullptr)
            {
                errno = EBADF;
                return std::nullopt;
            }

            if (fd->file->path.dentry->inode->stat.type() != stat::type::s_ifdir)
            {
                errno = ENOTDIR;
                return std::nullopt;
            }

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
            {
                errno = map_error(res.error());
                return std::nullopt;
            }
            return res.value();
        }

        std::optional<lib::path> get_path(const char __user *pathname)
        {
            const auto pathname_len = lib::strnlen_user(pathname, vfs::path_max);
            if (pathname_len == 0)
            {
                errno = EINVAL;
                return std::nullopt;
            }
            if (pathname_len == vfs::path_max)
            {
                errno = ENAMETOOLONG;
                return std::nullopt;
            }

            lib::path path { pathname_len, 0 };
            lib::bug_on(path.str().size() != pathname_len);

            lib::strncpy_from_user(path.str().data(), pathname, pathname_len);

            path.normalise();
            return std::move(path);
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
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);
        auto &fdt = proc->fdt;

        const bool follow_links = (flags & o_nofollow) == 0;
        const bool write = is_write(flags);

        if ((flags & o_tmpfile) && !write)
        {
            errno = EINVAL;
            return -1;
        }

        if ((flags & o_directory) && (flags & o_creat))
        {
            errno = EINVAL;
            return -1;
        }

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
            {
                errno = ENOTDIR;
                return -1;
            }

            const auto created = vfs::create(parent->target, path.basename(), (mode & ~proc->umask));
            if (!created.has_value())
            {
                errno = map_error(created.error());
                return -1;
            }
            lib::bug_on(!created->dentry || created->dentry->inode);

            const auto &parent_stat = parent->target.dentry->inode->stat;
            auto &stat = created->dentry->inode->stat;

            stat.st_uid = proc->euid;
            if (parent_stat.mode() & fmode::s_isgid)
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
                {
                    errno = map_error(reduced.error());
                    return -1;
                }
                target = reduced.value();
            }
            else if (target.dentry->inode->stat.type() == stat::type::s_iflnk)
            {
                errno = ELOOP;
                return -1;
            }
        }

        auto &stat = target.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir && write)
        {
            errno = EISDIR;
            return -1;
        }

        if (stat.type() != stat::s_ifdir && (flags & o_directory))
        {
            errno = ENOTDIR;
            return -1;
        }

        if (flags & o_trunc)
        {
            if (!target.dentry->inode->trunc(0))
            {
                errno = EINVAL;
                return -1;
            }
            stat.update_time(stat::time::modify | stat::time::status);
        }

        auto fd = fdt.allocate_fd(filedesc::create(target, flags), 0, false);
        lib::panic_if(!fd.has_value(), "openat: failed to allocate fd");

        return fd.value();
    }

    int close(int fd)
    {
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);

        if (!proc->fdt.close(static_cast<std::size_t>(fd)))
        {
            errno = EBADF;
            return -1;
        }
        return 0;
    }

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);

        auto fdesc = proc->fdt.get(static_cast<std::size_t>(fd));
        if (!fdesc)
        {
            errno = EBADF;
            return -1;
        }

        auto &file = fdesc->file;
        if (!is_read(file->flags))
        {
            errno = EBADF;
            return -1;
        }

        auto &stat = file->path.dentry->inode->stat;
        if (stat.type() == stat::type::s_ifdir)
        {
            errno = EISDIR;
            return -1;
        }

        lib::membuffer buffer { count };
        const auto ret = fdesc->file->read(buffer.span());
        if (ret < 0)
        {
            errno = -ret;
            return -1;
        }

        if (ret > 0)
            lib::copy_to_user(buf, buffer.data(), static_cast<std::size_t>(ret));

        stat.update_time(stat::time::access);
        return ret;
    }

    std::ssize_t write(int fd, const void __user *buf, std::size_t count)
    {
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);

        auto fdesc = proc->fdt.get(static_cast<std::size_t>(fd));
        if (!fdesc)
        {
            errno = EBADF;
            return -1;
        }

        auto &file = fdesc->file;
        if (!is_write(file->flags))
        {
            errno = EBADF;
            return -1;
        }

        lib::membuffer buffer { count };
        lib::copy_from_user(buffer.data(), buf, count);

        const auto ret = fdesc->file->write(buffer.span());
        if (ret < 0)
        {
            errno = -ret;
            return -1;
        }

        // TODO: sync

        auto &stat = file->path.dentry->inode->stat;
        stat.update_time(stat::time::modify | stat::time::status);
        return ret;
    }

    off_t lseek(int fd, off_t offset, int whence)
    {
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);

        auto fdesc = proc->fdt.get(static_cast<std::size_t>(fd));
        if (!fdesc)
        {
            errno = EBADF;
            return -1;
        }

        auto &file = fdesc->file;
        auto &stat = file->path.dentry->inode->stat;
        const auto type = stat.type();
        if (type == stat::s_ifchr || type == stat::s_ifsock || type == stat::s_ififo)
        {
            errno = ESPIPE;
            return -1;
        }

        std::size_t new_offset = 0;
        switch (whence)
        {
            case seek_set:
                new_offset = offset;
                break;
            case seek_cur:
                if (file->offset + offset > std::numeric_limits<off_t>::max())
                {
                    errno = EOVERFLOW;
                    return -1;
                }
                new_offset = file->offset + offset;
                break;
            case seek_end:
            {
                const std::size_t size = stat.st_size;
                if (size + offset > std::numeric_limits<off_t>::max())
                {
                    errno = EOVERFLOW;
                    return -1;
                }
                new_offset = size + offset;
                break;
            }
            default:
                errno = EINVAL;
                return -1;
        }

        if constexpr (std::is_signed_v<off_t>)
        {
            if (static_cast<off_t>(new_offset) < 0)
            {
                errno = EOVERFLOW;
                return -1;
            }
        }

        return file->offset = new_offset;
    }
} // namespace syscall::vfs