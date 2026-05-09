// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.memory.virt;
import system.chrono;
import system.vfs.socket;
import system.vfs.pipe;
import system.vfs.dev;
import magic_enum;
import arch;
import frigg;

namespace syscall::vfs
{
    using namespace ::vfs;
    using namespace magic_enum::bitwise_operators;

    namespace
    {
        lib::expect<std::shared_ptr<filedesc>> get_fd(sched::process_t *proc, int fdnum)
        {
            if (fdnum < 0)
                return std::unexpected { lib::err::invalid_fd };
            if (auto fd = proc->fdt->get(fdnum))
                return fd;
            return std::unexpected { lib::err::invalid_fd };
        }

        lib::expect<path> get_parent(sched::process_t *proc, int dirfd, lib::path_view path)
        {
            if (path.is_absolute())
                return get_root(true);

            if (dirfd == at_fdcwd)
                return proc->vfs->cwd;

            auto fd = get_fd(proc, dirfd);
            if (!fd)
                return std::unexpected { fd.error() };

            if ((*fd)->file->path.dentry->inode->stat.type() != stat::type::s_ifdir)
                return std::unexpected { lib::err::not_a_dir };

            return (*fd)->file->path;
        }

        lib::expect<resolve_res> resolve_from(
            sched::process_t *proc, int dirfd,
            lib::path_view path, bool automount = true
        )
        {
            auto parent = get_parent(proc, dirfd, path);
            if (!parent)
                return std::unexpected { parent.error() };

            auto res = resolve(std::move(*parent), path, automount);
            if (!res)
                return std::unexpected { res.error() };

            return std::move(*res);
        }

        lib::expect<path> resolve_parent_dir(
            sched::process_t *proc, int dirfd, lib::path_view path
        )
        {
            auto res = resolve_from(proc, dirfd, path);
            if (!res.has_value())
                return std::unexpected { res.error() };

            if (res->target.dentry->inode->stat.type() == stat::type::s_iflnk)
            {
                auto reduced = reduce(res->parent, res->target);
                if (!reduced.has_value())
                    return std::unexpected { reduced.error() };
                return std::move(*reduced);
            }
            return std::move(res->target);
        }

        lib::expect<lib::path> get_path(const char __user *pathname)
        {
            if (pathname == nullptr)
                return std::unexpected { lib::err::invalid_address };

            const auto pathname_len = lib::strnlen_user(pathname, path_max);
            if (pathname_len < 0)
                return std::unexpected { lib::err::invalid_address };
            if (pathname_len == 0)
                return std::unexpected { lib::err::invalid_path };
            if (pathname_len == path_max)
                return std::unexpected { lib::err::path_too_long };

            lib::path path { static_cast<std::size_t>(pathname_len), 0 };
            lib::bug_on(path.str().size() != static_cast<std::size_t>(pathname_len));

            if (!lib::copy_from_user(path.str().data(), pathname, pathname_len))
                return std::unexpected { lib::err::invalid_address };

            path.normalise();
            return path;
        }

        int close_fd(sched::process_t *proc, int fd, bool was_opened = true)
        {
            if (!was_opened)
            {
                const auto fdesc = get_fd(proc, fd);
                if (!fdesc)
                    return -lib::map_error(fdesc.error());
                proc->fdt->fds.write_lock()->erase(fd);
                return 0;
            }

            if (!proc->fdt->close(fd))
                return -EBADF;

            return 0;
        }

        std::uint64_t mount_flags(const path &path)
        {
            return path.mnt ? path.mnt->flags : 0ul;
        }

        bool readonly_mount(const path &path)
        {
            return (mount_flags(path) & ms_rdonly) != 0;
        }

        bool should_update_atime(const path &path, const kstat &stat, int file_flags = 0)
        {
            if (file_flags & o_noatime)
                return false;

            const auto flags = mount_flags(path);
            if (flags & ms_noatime)
                return false;
            if ((flags & ms_nodiratime) && stat.type() == stat::type::s_ifdir)
                return false;
            if (flags & ms_strictatime)
                return true;

            const auto &atim = stat.st_atim;
            if (atim < stat.st_mtim || atim < stat.st_ctim)
                return true;

            constexpr timespec day { 24 * 60 * 60, 0 };
            return chrono::now(chrono::realtime) - atim >= day;
        }

        int touch_atime(const std::shared_ptr<vfs::file> &file)
        {
            auto &inode = file->path.dentry->inode;
            const std::unique_lock _ { inode->lock };
            if (!should_update_atime(file->path, inode->stat, file->flags))
                return 0;

            inode->stat.update_time(kstat::time::access);
            if (const auto ret = dirty_inode(file->path); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }
    } // namespace

    lib::expect<path> get_target(
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
                    return std::unexpected { lib::err::invalid_address };
                use_dirfd = (pathname_len == 0);
            }

            if (use_dirfd)
            {
                if (dirfd == at_fdcwd)
                    return proc->vfs->cwd;

                auto fd = get_fd(proc, dirfd);
                if (!fd)
                    return std::unexpected { fd.error() };

                return (*fd)->file->path;
            }
        }

        auto val = get_path(pathname);
        if (!val)
            return std::unexpected { val.error() };

        auto res = resolve_from(proc, dirfd, std::move(*val), automount);
        if (!res)
            return std::unexpected { res.error() };

        auto target = std::move(res->target);
        lib::bug_on(!target.dentry || !target.dentry->inode);

        if (follow_links)
        {
            auto reduced = reduce(std::move(res->parent), std::move(target), automount);
            if (!reduced)
                return std::unexpected { reduced.error() };
            target = std::move(*reduced);
        }
        return target;
    }

    mode_t umask(mode_t mask)
    {
        const auto proc = sched::current_process();
        const auto ret = proc->vfs->umask;
        proc->vfs->umask = mask & 0777;
        return ret;
    }

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

        auto val = get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto pathstr = std::move(*val);

        bool did_create = false;

        path target { };
        auto res = resolve_from(proc, dirfd, pathstr);
        if (!res.has_value())
        {
            if ((flags & o_creat) == 0)
                return -lib::map_error(res.error());

            const auto parent = resolve_parent_dir(proc, dirfd, pathstr.dirname());
            if (!parent.has_value())
                return -lib::map_error(parent.error());

            const auto &parent_stat = parent->dentry->inode->stat;
            if (parent_stat.type() != stat::type::s_ifdir)
                return -ENOTDIR;

            if (readonly_mount(*parent))
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
        const auto mflags = mount_flags(target);
        const bool is_tmpfile = (flags & o_tmpfile) == o_tmpfile;
        const bool needs_write = is_tmpfile || ((write || trunc) && !did_create);

        if (needs_write && (mflags & ms_rdonly))
            return -EROFS;

        if (stat.type() == stat::type::s_ifdir && (write || trunc) && !is_tmpfile)
            return -EISDIR;

        if (stat.type() != stat::s_ifdir && (flags & o_directory))
            return -ENOTDIR;

        if ((mflags & ms_nodev) &&
            (stat.type() == stat::type::s_ifchr || stat.type() == stat::type::s_ifblk))
            return -EACCES;

        if ((flags & o_noatime) &&
            proc->cred->fsuid != stat.st_uid &&
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
            close_fd(proc, *fdres, false);
            return -lib::map_error(ret.error());
        }

        if (trunc)
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
        const auto proc = sched::current_process();
        return close_fd(proc, fd);
    }

    int close_range(std::uint32_t first, std::uint32_t last, std::uint32_t flags)
    {
        enum : std::uint32_t {
            unshare = 1 << 1,
            cloexec = 1 << 2
        };

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
            auto wlocked = fdt->fds.write_lock();
            for (auto &[fd, fdesc] : *wlocked)
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

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return -EBADF;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = fdesc->file->read(*uspan);
        if (!ret.has_value())
            return -lib::map_error(ret.error());

        if (const auto err = touch_atime(file); err < 0)
            return err;

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return *ret;
    }

    std::ssize_t write(int fd, const void __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (readonly_mount(file->path))
            return -EROFS;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = fdesc->file->write(*uspan);
        if (!ret.has_value())
            return -lib::map_error(ret.error());

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(kstat::time::modify | kstat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return -lib::map_error(ret.error());
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return *ret;
    }

    std::ssize_t pread(int fd, void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return -EBADF;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), *uspan);
        if (!ret.has_value())
            return -lib::map_error(ret.error());

        if (const auto err = touch_atime(file); err < 0)
            return err;

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return *ret;
    }

    std::ssize_t pwrite(int fd, const void __user *buf, std::size_t count, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (readonly_mount(file->path))
            return -EROFS;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        if (stat.type() == stat::type::s_ifreg)
        {
            const auto fsize = proc->rlimits->get(sched::rlimit_fsize).cur;
            if (static_cast<rlim_t>(offset) >= fsize ||
                static_cast<rlim_t>(offset) + count > fsize)
                return -EFBIG;
        }

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), *uspan);
        if (!ret.has_value())
            return -lib::map_error(ret.error());

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(kstat::time::modify | kstat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return -lib::map_error(ret.error());
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return *ret;
    }

    std::ssize_t readv(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return -EBADF;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        std::size_t total_read = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return -EFAULT;

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return -EFAULT;

            const auto ret = fdesc->file->read(*uspan);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            if (*ret == 0)
                break;

            total_read += *ret;
        }

        if (const auto err = touch_atime(file); err < 0)
            return err;

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t writev(int fd, const iovec __user *iov, int iovcnt)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (readonly_mount(file->path))
            return -EROFS;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        std::size_t total_written = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return -EFAULT;

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return -EFAULT;

            const auto ret = fdesc->file->write(*uspan);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            total_written += *ret;
        }

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(kstat::time::modify | kstat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return -lib::map_error(ret.error());
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return static_cast<std::ssize_t>(total_written);
    }

    std::ssize_t preadv(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_read(file->flags))
            return -EBADF;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        std::size_t total_read = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return -EFAULT;

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return -EFAULT;

            const auto ret = fdesc->file->pread(static_cast<std::uint64_t>(offset), *uspan);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            if (*ret == 0)
                break;

            total_read += *ret;
            offset += static_cast<off_t>(*ret);
        }

        if (const auto err = touch_atime(file); err < 0)
            return err;

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return static_cast<std::ssize_t>(total_read);
    }

    std::ssize_t pwritev(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (readonly_mount(file->path))
            return -EROFS;

        auto &dentry = file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() == stat::type::s_ifdir)
            return -EISDIR;

        const bool check_fsize = stat.type() == stat::type::s_ifreg;
        const auto fsize = proc->rlimits->get(sched::rlimit_fsize).cur;

        std::size_t total_written = 0;
        for (int i = 0; i < iovcnt; i++)
        {
            iovec local_iov;
            if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                return -EFAULT;

            if (check_fsize && (static_cast<rlim_t>(offset) >= fsize ||
                static_cast<rlim_t>(offset) + local_iov.iov_len > fsize))
            {
                if (total_written == 0)
                    return -EFBIG;
                break;
            }

            auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
            if (!uspan.has_value())
                return -EFAULT;

            const auto ret = fdesc->file->pwrite(static_cast<std::uint64_t>(offset), *uspan);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            total_written += *ret;
            offset += static_cast<off_t>(*ret);
        }

        {
            const std::unique_lock _ { inode->lock };
            stat.update_time(kstat::time::modify | kstat::time::status);

            if (const auto ret = dirty_inode(file->path); !ret)
                return -lib::map_error(ret.error());
        }

        if (file->flags & (o_sync | o_dsync))
        {
            if (const auto ret = file->sync(); !ret)
                return -lib::map_error(ret.error());
        }

        return static_cast<std::ssize_t>(total_written);
    }

    // std::ssize_t preadv2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);
    // std::ssize_t pwritev2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);

    off_t lseek(int fd, off_t offset, int whence)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        const auto &stat = file->path.dentry->inode->stat;
        const auto type = stat.type();
        if (type == stat::s_ifsock || type == stat::s_ififo)
            return -ESPIPE;

        if (const auto ops_res = file->get_ops(); !ops_res || !(*ops_res)->seekable())
            return -ESPIPE;

        std::size_t new_offset = 0;
        switch (whence)
        {
            case seek_set:
                new_offset = offset;
                break;
            case seek_cur:
                if (file->offset + offset > std::numeric_limits<off_t>::max())
                    return -EOVERFLOW;
                new_offset = file->offset + offset;
                break;
            case seek_end:
            {
                const std::size_t size = stat.st_size;
                if (size + offset > std::numeric_limits<off_t>::max())
                    return -EOVERFLOW;
                new_offset = size + offset;
                break;
            }
            default:
                return -EINVAL;
        }

        if constexpr (std::is_signed_v<off_t>)
        {
            if (static_cast<off_t>(new_offset) < 0)
                return -EOVERFLOW;
        }

        return file->offset = new_offset;
    }

    int fstatat(int dirfd, const char __user *pathname, ::stat __user *statbuf, int flags)
    {
        const auto proc = sched::current_process();

        if (flags & ~(at_symlink_nofollow | at_no_automount | at_empty_path))
            return -EINVAL;

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool automount = true; // (flags & at_no_automount) == 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, automount);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (!lib::copy_to_user(statbuf, &target->dentry->inode->stat, sizeof(::stat)))
            return -EFAULT;
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

    int statx(int dirfd, const char __user *pathname, int flags, std::uint32_t mask, struct statx __user *statxbuf)
    {
        const auto proc = sched::current_process();

        constexpr auto valid_flags =
            at_symlink_nofollow |
            at_no_automount |
            at_empty_path |
            at_statx_sync_type;

        if ((flags & ~valid_flags) != 0)
            return -EINVAL;

        const auto sync_mode = flags & at_statx_sync_type;
        if (sync_mode == (at_statx_force_sync | at_statx_dont_sync))
            return -EINVAL;

        constexpr std::uint32_t statx_basic_stats = 0x000007FFu;
        constexpr std::uint32_t statx_btime = 0x00000800u;
        constexpr std::uint32_t statx_mnt_id = 0x00001000u;
        constexpr std::uint32_t statx_reserved = 0x80000000u;
        constexpr std::uint64_t statx_attr_mount_root = 0x00002000u;

        if (mask & statx_reserved)
            return -EINVAL;

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool automount = (flags & at_no_automount) == 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, automount);
        if (!target.has_value())
            return -lib::map_error(target.error());

        kstat val;
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
        ret.stx_mask = statx_basic_stats | statx_btime;
        ret.stx_blksize = static_cast<std::uint32_t>(val.st_blksize);
        ret.stx_nlink = static_cast<std::uint32_t>(val.st_nlink);
        ret.stx_uid = static_cast<std::uint32_t>(val.st_uid);
        ret.stx_gid = static_cast<std::uint32_t>(val.st_gid);
        ret.stx_mode = static_cast<std::uint16_t>(val.st_mode);
        ret.stx_ino = static_cast<std::uint64_t>(val.st_ino);
        ret.stx_size = static_cast<std::uint64_t>(val.st_size);
        ret.stx_blocks = static_cast<std::uint64_t>(val.st_blocks);
        ret.stx_atime = to_statx_timestamp(val.st_atim);
        ret.stx_btime = to_statx_timestamp(val.st_btim);
        ret.stx_ctime = to_statx_timestamp(val.st_ctim);
        ret.stx_mtime = to_statx_timestamp(val.st_mtim);
        ret.stx_rdev_major = dev::major(val.st_rdev);
        ret.stx_rdev_minor = dev::minor(val.st_rdev);
        ret.stx_dev_major = dev::major(val.st_dev);
        ret.stx_dev_minor = dev::minor(val.st_dev);

        if (target->mnt != nullptr)
        {
            ret.stx_mask |= statx_mnt_id;
            ret.stx_mnt_id = target->mnt->fs.lock()->dev_id;

            ret.stx_attributes_mask |= statx_attr_mount_root;
            if (target->dentry == target->mnt->root)
                ret.stx_attributes |= statx_attr_mount_root;
        }

        if (!lib::copy_to_user(statxbuf, &ret, sizeof(struct statx)))
            return -EFAULT;

        return 0;
    }

    namespace
    {
        int do_statfs(const path &target, struct statfs __user *buf)
        {
            if (!target.mnt)
                return -ENOSYS;

            struct statfs out { };
            {
                auto fs = target.mnt->fs.lock();
                fs->statfs(out);
            }

            constexpr std::uint64_t flag_mask = ms_rdonly | ms_nosuid | ms_nodev | ms_noexec |
                ms_synchronous | ms_mandlock | ms_noatime | ms_nodiratime | ms_relatime;
            out.f_flags = static_cast<std::int64_t>(target.mnt->flags & flag_mask);

            if (!lib::copy_to_user(buf, &out, sizeof(out)))
                return -EFAULT;
            return 0;
        }
    } // namespace

    int statfs(const char __user *path, struct statfs __user *buf)
    {
        const auto proc = sched::current_process();
        const auto target = get_target(proc, at_fdcwd, path, true, false, true);
        if (!target.has_value())
            return -lib::map_error(target.error());
        return do_statfs(*target, buf);
    }

    int fstatfs(int fd, struct statfs __user *buf)
    {
        const auto proc = sched::current_process();
        auto fdesc = get_fd(proc, fd);
        if (!fdesc)
            return -lib::map_error(fdesc.error());
        if (!(*fdesc)->file)
            return -EBADF;
        return do_statfs((*fdesc)->file->path, buf);
    }

    int faccessat2(int dirfd, const char __user *pathname, int mode, int flags)
    {
        if ((mode & ~(f_ok | x_ok | w_ok | r_ok)) != 0)
            return -EINVAL;

        if ((flags & ~(at_symlink_nofollow | at_empty_path | at_eaccess)) != 0)
            return -EINVAL;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool eaccess = (flags & at_eaccess) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

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
                cred->effective &= ~(sched::cap_t::dac_override | sched::cap_t::dac_read_search);
        }
        else cred = proc->cred;

        if (!vfs::check_access(*target, cred, static_cast<std::uint32_t>(desired)))
            return -EACCES;

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

    namespace
    {
        int do_fchmodat(int dirfd, const char __user *pathname, mode_t mode, int flags)
        {
            if (flags & ~(at_symlink_nofollow | at_empty_path))
                return -EINVAL;

            const auto proc = sched::current_process();

            const bool follow_links = (flags & at_symlink_nofollow) == 0;
            const bool empty_path = (flags & at_empty_path) != 0;

            const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
            if (!target.has_value())
                return -lib::map_error(target.error());

            if (readonly_mount(*target))
                return -EROFS;

            auto &inode = target->dentry->inode;
            {
                const std::unique_lock _ { inode->lock };

                const auto &cred = proc->cred;
                if (cred->fsuid != inode->stat.st_uid && !sched::capable(cred, sched::cap_t::fowner))
                    return -EPERM;

                if ((mode & s_isgid) && cred->fsgid != inode->stat.st_gid &&
                    !cred->supp_gids.contains(inode->stat.st_gid) &&
                    !sched::capable(cred, sched::cap_t::fsetid))
                    mode &= ~s_isgid;

                constexpr auto bits = (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

                inode->stat.st_mode = (inode->stat.st_mode & ~bits) | (mode & bits);
                inode->stat.update_time(kstat::time::status);

                if (const auto ret = dirty_inode(*target); !ret)
                    return -lib::map_error(ret.error());
            }
            return 0;
        }
    }

    int fchmodat(int dirfd, const char __user *pathname, mode_t mode)
    {
        return do_fchmodat(dirfd, pathname, mode, 0);
    }

    int fchmodat2(int dirfd, const char __user *pathname, mode_t mode, int flags)
    {
        return do_fchmodat(dirfd, pathname, mode, flags);
    }

    int chmod(const char __user *pathname, mode_t mode)
    {
        return do_fchmodat(at_fdcwd, pathname, mode, 0);
    }

    int fchmod(int fd, mode_t mode)
    {
        return do_fchmodat(fd, nullptr, mode, at_empty_path);
    }

    int fchownat(int dirfd, const char __user *pathname, uid_t owner, gid_t group, int flags)
    {
        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return -EINVAL;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (readonly_mount(*target))
            return -EROFS;

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
                    return -EPERM;

                const auto old_uid = inode->stat.st_uid;
                const auto old_gid = inode->stat.st_gid;
                const auto old_mode = inode->stat.st_mode;

                if (change_uid && !uid_noop && inode->xattrs.contains(xattr_caps_name))
                {
                    lib::bug_on(!target->mnt);

                    auto fs = target->mnt->fs.lock();
                    if (!fs.get())
                        return -EIO;

                    if (const auto ret = fs->remxattr(inode, xattr_caps_name); !ret)
                        return -lib::map_error(ret.error());

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

                inode->stat.update_time(kstat::time::status);
                if (const auto ret = dirty_inode(*target); !ret)
                {
                    // TODO: restore xattr
                    inode->stat.st_uid = old_uid;
                    inode->stat.st_gid = old_gid;
                    inode->stat.st_mode = old_mode;
                    return -lib::map_error(ret.error());
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

        auto val = get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto path = std::move(*val);
        if (resolve_from(proc, dirfd, path).has_value())
            return -EEXIST;

        const auto parent = resolve_parent_dir(proc, dirfd, path.dirname());
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        const auto &parent_stat = parent->dentry->inode->stat;
        if (parent_stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (readonly_mount(*parent))
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

        auto val = get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto path = std::move(*val);

        const auto parent = resolve_parent_dir(proc, dirfd, path.dirname());
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        if (readonly_mount(*parent))
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
            if (cred->fsuid != tstat.st_uid &&
                cred->fsuid != parent_stat.st_uid &&
                !sched::capable(cred, sched::cap_t::fowner))
                return -EACCES;
        }

        if (const auto ret = unlink(*parent, path.basename()); !ret)
            return -lib::map_error(ret.error());

        return 0;
    }

    int unlink(const char __user *pathname)
    {
        return unlinkat(at_fdcwd, pathname, 0);
    }

    int rmdir(const char __user *pathname)
    {
        return unlinkat(at_fdcwd, pathname, at_removedir);
    }

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

        auto val = get_path(pathname);
        if (!val.has_value())
            return -lib::map_error(val.error());

        const auto path = std::move(*val);
        if (resolve_from(proc, dirfd, path).has_value())
            return -EEXIST;

        const auto parent = resolve_parent_dir(proc, dirfd, path.dirname());
        if (!parent.has_value())
            return -lib::map_error(parent.error());

        const auto &parent_stat = parent->dentry->inode->stat;
        if (parent_stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (readonly_mount(*parent))
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
        int olddirfd, const char __user *oldpath,
        int newdirfd, const char __user *newpath, int flags
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

        auto new_val = get_path(newpath);
        if (!new_val.has_value())
            return -lib::map_error(new_val.error());
        const auto new_path = std::move(*new_val);

        const auto new_anchor = get_parent(proc, newdirfd, new_path);
        if (!new_anchor.has_value())
            return -lib::map_error(new_anchor.error());

        const auto new_parent = resolve_parent_dir(proc, newdirfd, new_path.dirname());
        if (!new_parent.has_value())
            return -lib::map_error(new_parent.error());

        if (readonly_mount(*new_parent))
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

        auto target_val = get_path(target);
        if (!target_val.has_value())
            return -lib::map_error(target_val.error());

        auto link_val = get_path(linkpath);
        if (!link_val.has_value())
            return -lib::map_error(link_val.error());

        const auto link = std::move(*link_val);

        const auto parent = get_parent(proc, newdirfd, link);
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

        if (readonly_mount(parent_dir))
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

    int mount(
        const char __user *source, const char __user *target,
        const char __user *fstype, std::uint64_t flags, const void __user *data
    )
    {
        const auto proc = sched::current_process();

        if (!sched::capable(proc->cred, sched::cap_t::sys_admin))
            return -EPERM;

        if (target == nullptr)
            return -EFAULT;

        auto target_val = get_path(target);
        if (!target_val.has_value())
            return -lib::map_error(target_val.error());

        // TODO
        if (flags & (ms_shared | ms_private | ms_slave | ms_unbindable))
            return 0;

        std::optional<std::string> fstype_str;
        if (fstype != nullptr)
        {
            fstype_str = lib::user_string::get(fstype, 256);
            if (!fstype_str.has_value())
                return -EINVAL;
        }
        else if (!(flags & ms_remount))
            return -EFAULT;

        lib::path source_path { };
        if (source != nullptr)
        {
            auto val = get_path(source);
            if (val.has_value())
                source_path = std::move(*val);
            else if (val.error() != lib::err::invalid_path)
                return -lib::map_error(val.error());
        }

        constexpr std::size_t data_max = 4096;
        std::array<std::byte, data_max> data_buf { };
        std::optional<lib::maybe_uspan<const std::byte>> data_uspan { };
        if (data != nullptr)
        {
            const auto probe = lib::strnlen_user(static_cast<const char __user *>(data), data_max);
            if (probe < 0)
                return -EFAULT;

            if (probe > 0 && !lib::copy_from_user(data_buf.data(), data, probe))
                return -EFAULT;

            const auto uspan = lib::maybe_uspan<const std::byte>::create(data_buf.data(), data_max);
            if (!uspan.has_value())
                return -EFAULT;
            data_uspan = *uspan;
        }

        const std::string_view fstype_sv = fstype_str ? *fstype_str : std::string_view { };
        if (const auto ret = ::vfs::mount(source_path, *target_val, fstype_sv, flags, data_uspan); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int renameat(int olddirfd, const char __user *oldpath, int newdirfd, const char __user *newpath)
    {
        const auto proc = sched::current_process();

        auto old_val = get_path(oldpath);
        if (!old_val.has_value())
            return -lib::map_error(old_val.error());
        const auto old_path = std::move(*old_val);

        auto new_val = get_path(newpath);
        if (!new_val.has_value())
            return -lib::map_error(new_val.error());
        const auto new_path = std::move(*new_val);

        const auto old_anchor = get_parent(proc, olddirfd, old_path);
        if (!old_anchor.has_value())
            return -lib::map_error(old_anchor.error());

        const auto new_anchor = get_parent(proc, newdirfd, new_path);
        if (!new_anchor.has_value())
            return -lib::map_error(new_anchor.error());

        const auto old_parent = resolve_parent_dir(proc, olddirfd, old_path.dirname());
        if (!old_parent.has_value())
            return -lib::map_error(old_parent.error());

        const auto new_parent = resolve_parent_dir(proc, newdirfd, new_path.dirname());
        if (!new_parent.has_value())
            return -lib::map_error(new_parent.error());

        if (readonly_mount(*old_parent) || readonly_mount(*new_parent))
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

        if (const auto tgt = resolve_from(proc, olddirfd, old_path); tgt.has_value())
        {
            if (!sticky_ok(*old_parent, tgt->target))
                return -EACCES;
        }
        if (const auto tgt = resolve_from(proc, newdirfd, new_path); tgt.has_value())
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
        else ktimes[0] = ktimes[1] = now;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (readonly_mount(*target))
            return -EROFS;

        auto &inode = target->dentry->inode;
        auto &stat = inode->stat;

        const auto &cred = proc->cred;
        const bool is_owner = (cred->fsuid == stat.st_uid);
        const bool has_fowner = sched::capable(cred, sched::cap_t::fowner);

        const auto is_special = [](const auto ns) {
            return ns == utime_now || ns == utime_omit;
        };

        if (times && (!is_special(ktimes[0].tv_nsec) || !is_special(ktimes[1].tv_nsec)))
        {
            if (!is_owner && !has_fowner)
                return -EPERM;
        }
        else if (!is_owner && !has_fowner &&
            !vfs::check_access(*target, proc->cred,
                static_cast<std::uint32_t>(sched::access_mode::write)))
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

    int fsync(int fd)
    {
        // TODO
        lib::unused(fd);
        return 0;
    }

    namespace
    {
        int do_trunc(const path &target, off_t length)
        {
            auto &inode = target.dentry->inode;
            if (inode->stat.type() == stat::type::s_ifdir)
                return -EISDIR;
            if (inode->stat.type() != stat::type::s_ifreg)
                return -EINVAL;

            if (readonly_mount(target))
                return -EROFS;

            auto file = vfs::file::create(target, 0, o_wronly);
            if (const auto ret = file->trunc(static_cast<std::size_t>(length)); !ret)
                return -lib::map_error(ret.error());

            const std::unique_lock _ { inode->lock };
            inode->stat.update_time(kstat::time::modify | kstat::time::status);
            if (const auto ret = dirty_inode(target); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }
    } // namespace

    int truncate(const char __user *pathname, off_t length)
    {
        if (length < 0)
            return -EINVAL;

        const auto proc = sched::current_process();
        if (static_cast<rlim_t>(length) > proc->rlimits->get(sched::rlimit_fsize).cur)
            return -EFBIG;

        const auto target = get_target(proc, at_fdcwd, pathname, true, false, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (!vfs::check_access(*target, proc->cred,
            static_cast<std::uint32_t>(sched::access_mode::write)))
            return -EACCES;

        return do_trunc(*target, length);
    }

    int ftruncate(int fd, off_t length)
    {
        if (length < 0)
            return -EINVAL;

        const auto proc = sched::current_process();
        if (static_cast<rlim_t>(length) > proc->rlimits->get(sched::rlimit_fsize).cur)
            return -EFBIG;

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        if (!is_write(fdesc->file->flags))
            return -EINVAL;

        return do_trunc(fdesc->file->path, length);
    }

    int ioctl(int fd, std::uint64_t request, void __user *argp)
    {
        constexpr std::uint64_t fionbio = 0x5421;
        constexpr std::uint64_t fioclex = 0x5451;
        constexpr std::uint64_t fionclex = 0x5450;

        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        switch (request)
        {
            case fionbio:
            {
                int value = 0;
                if (!lib::copy_from_user(&value, argp, sizeof(value)))
                    return -EFAULT;
                if (value)
                    fdesc->file->flags |= o_nonblock;
                else
                    fdesc->file->flags &= ~o_nonblock;
                return 0;
            }
            case fioclex:
                fdesc->closexec.store(true, std::memory_order_relaxed);
                return 0;
            case fionclex:
                fdesc->closexec.store(false, std::memory_order_relaxed);
                return 0;
        }

        const auto ret = fdesc->file->ioctl(request, lib::uptr_or_addr { argp });
        if (!ret.has_value())
            return -lib::map_error(ret.error());
        return *ret;
    }

    int fcntl(int fd, int cmd, std::uintptr_t arg)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        switch (cmd)
        {
            case 0: // F_DUPFD
            {
                const auto fdres = proc->fdt->dup(
                    fd, static_cast<int>(arg), false, false,
                    proc->rlimits->get(sched::rlimit_nofile).cur
                );
                if (!fdres.has_value())
                    return -lib::map_error(fdres.error());
                return *fdres;
            }
            case 1030: // F_DUPFD_CLOEXEC
            {
                const auto fdres = proc->fdt->dup(
                    fd, static_cast<int>(arg), true, false,
                    proc->rlimits->get(sched::rlimit_nofile).cur
                );
                if (!fdres.has_value())
                    return -lib::map_error(fdres.error());
                return *fdres;
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
            case 1031: // F_SETPIPE_SZ
            {
                const auto ret = vfs::pipe::set_size(fdesc->file, static_cast<std::size_t>(arg));
                if (!ret)
                    return -lib::map_error(ret.error());
                return static_cast<int>(*ret);
            }
            case 1032: // F_GETPIPE_SZ
            {
                const auto ret = vfs::pipe::get_size(fdesc->file);
                if (!ret)
                    return -lib::map_error(ret.error());
                return static_cast<int>(*ret);
            }
            // TODO: flock
            case 6: // F_SETLK
            case 7: // F_SETLKW
            {
                struct flock
                {
                    std::int16_t l_type;
                    std::int16_t l_whence;
                    off_t l_start;
                    off_t l_len;
                    pid_t l_pid;
                };

                flock fl;
                if (!lib::copy_from_user(&fl, reinterpret_cast<const flock __user *>(arg), sizeof(fl)))
                    return -EFAULT;

                // F_RDLCK, F_WRLCK, F_UNLCK
                if (fl.l_type != 0 && fl.l_type != 1 && fl.l_type != 2)
                    return -EINVAL;

                if (fl.l_whence != seek_set && fl.l_whence != seek_cur && fl.l_whence != seek_end)
                    return -EINVAL;

                // TODO: actually enforce locks
                break;
            }
            default:
                lib::error("fcntl: unhandled command: 0x{:X}", cmd);
                return -EINVAL;
        }
        return 0;
    }

    int flock(int fd, int operation)
    {
        // TODO: actually enforce locks
        constexpr int lock_sh = 1;
        constexpr int lock_ex = 2;
        constexpr int lock_un = 8;
        constexpr int lock_nb = 4;

        if ((operation & ~lock_nb) != lock_sh &&
            (operation & ~lock_nb) != lock_ex &&
            (operation & ~lock_nb) != lock_un)
            return -EINVAL;

        const auto proc = sched::current_process();
        const auto fdesc = get_fd(proc, fd);
        if (!fdesc)
            return -lib::map_error(fdesc.error());
        return 0;
    }

    int dup(int oldfd)
    {
        const auto proc = sched::current_process();
        const auto fdres = proc->fdt->dup(
            oldfd, 0, false, false,
            proc->rlimits->get(sched::rlimit_nofile).cur
        );
        if (!fdres.has_value())
            return -lib::map_error(fdres.error());
        return *fdres;
    }

    int dup2(int oldfd, int newfd)
    {
        const auto proc = sched::current_process();
        const auto fdres = proc->fdt->dup(
            oldfd, newfd, false, true,
            proc->rlimits->get(sched::rlimit_nofile).cur
        );
        if (!fdres.has_value())
            return -lib::map_error(fdres.error());
        return *fdres;
    }

    int dup3(int oldfd, int newfd, int flags)
    {
        if (oldfd == newfd || (flags & ~o_closexec) != 0)
            return -EINVAL;

        const auto proc = sched::current_process();
        const auto fdres = proc->fdt->dup(
            oldfd, newfd, (flags & o_closexec) != 0, true,
            proc->rlimits->get(sched::rlimit_nofile).cur
        );
        if (!fdres.has_value())
            return -lib::map_error(fdres.error());
        return *fdres;
    }

    int getcwd(char __user *buf, std::size_t size)
    {
        const auto proc = sched::current_process();

        const auto path_str = pathname_from(proc->vfs->cwd);
        const auto len = path_str.size() + 1;
        if (len > size)
            return -ERANGE;

        if (!lib::copy_to_user(buf, path_str.c_str(), len))
            return -EFAULT;

        return len;
    }

    int chdir(const char __user *pathname)
    {
        const auto proc = sched::current_process();

        const auto target = get_target(proc, at_fdcwd, pathname, true, false, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto &stat = target->dentry->inode->stat;
        if (stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (!vfs::check_access(*target, proc->cred,
            static_cast<std::uint32_t>(sched::access_mode::exec)))
            return -EACCES;

        proc->vfs->cwd = *target;
        return 0;
    }

    int fchdir(int fd)
    {
        const auto proc = sched::current_process();

        const auto target = get_target(proc, fd, nullptr, true, true, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto &stat = target->dentry->inode->stat;
        if (stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        if (!vfs::check_access(*target, proc->cred,
            static_cast<std::uint32_t>(sched::access_mode::exec)))
            return -EACCES;

        proc->vfs->cwd = *target;
        return 0;
    }

    int pipe2(int __user *pipefd, int flags)
    {
        const auto proc = sched::current_process();
        auto &fdt = proc->fdt;

        if (flags & ~(o_closexec | o_nonblock))
            return -EINVAL;

        auto shared_inode = std::make_shared<inode>();
        {
            shared_inode->stat.st_ino = vfs::next_anon_ino();
            shared_inode->stat.st_blksize = 0x1000;
            shared_inode->stat.st_mode = std::to_underlying(stat::s_ififo) | s_irwxu | s_irwxg | s_irwxo;
            shared_inode->stat.st_uid = proc->cred->euid;
            shared_inode->stat.st_gid = proc->cred->egid;

            shared_inode->stat.update_time(
                kstat::time::access |
                kstat::time::modify |
                kstat::time::status |
                kstat::time::birth
            );
        }

        vfs::pipe::prep_anon(shared_inode);

        std::array<int, 2> fds;

        const auto rdentry = std::make_shared<dentry>();
        rdentry->name = "<[PIPE READ]>";
        rdentry->inode = shared_inode;

        const auto rfdesc = filedesc::create({
            .dentry = rdentry,
            .mnt = nullptr
        }, flags | o_rdonly);

        rfdesc->closexec = (flags & o_closexec) != 0;

        const auto max_fd = proc->rlimits->get(sched::rlimit_nofile).cur;

        const auto fd0res = fdt->alloc(rfdesc, 0, false, max_fd);
        if (!fd0res.has_value())
            return -lib::map_error(fd0res.error());
        fds[0] = *fd0res;

        if (const auto ret = rfdesc->file->open(flags | o_rdonly, proc->pid); !ret)
        {
            close_fd(proc, fds[0], false);
            return -lib::map_error(ret.error());
        }

        const auto wdentry = std::make_shared<dentry>();
        wdentry->name = "<[PIPE WRITE]>";
        wdentry->inode = std::move(shared_inode);

        const auto wfdesc = filedesc::create({
            .dentry = wdentry,
            .mnt = nullptr
        }, flags | o_wronly);

        wfdesc->closexec = (flags & o_closexec) != 0;

        const auto fd1res = fdt->alloc(wfdesc, 0, false, max_fd);
        if (!fd1res.has_value())
        {
            close_fd(proc, fds[0]);
            return -lib::map_error(fd1res.error());
        }
        fds[1] = *fd1res;

        if (const auto ret = wfdesc->file->open(flags | o_wronly, proc->pid); !ret)
        {
            close_fd(proc, fds[0]);
            close_fd(proc, fds[1], false);
            return -lib::map_error(ret.error());
        }

        if (!lib::copy_to_user(pipefd, fds.data(), sizeof(int) * 2))
        {
            close_fd(proc, fds[0]);
            close_fd(proc, fds[1]);
            return -EFAULT;
        }
        return 0;
    }

    int pipe(int __user *pipefd)
    {
        return pipe2(pipefd, 0);
    }

    namespace
    {
        bool is_socket(const std::shared_ptr<vfs::filedesc> &fdesc)
        {
            if (!fdesc->file || !fdesc->file->path.dentry)
                return false;

            const auto &inode = fdesc->file->path.dentry->inode;
            if (!inode)
                return false;

            return inode->stat.type() == stat::s_ifsock;
        }

        auto get_socket(sched::process_t *proc, int sockfd)
            -> lib::expect<std::shared_ptr<socket::socket_t>>
        {
            const auto fdesc_res = get_fd(proc, sockfd);
            if (!fdesc_res)
                return std::unexpected { fdesc_res.error() };

            const auto &fdesc = *fdesc_res;
            if (!is_socket(fdesc))
                return std::unexpected { lib::err::not_a_socket };

            return socket::from_file(*fdesc->file);
        }

        int map_flags(int flags)
        {
            // these are the same but ehh
            int ret = 0;
            if (flags & sock_cloexec)
                ret |= o_closexec;
            if (flags & sock_nonblock)
                ret |= o_nonblock;
            return ret;
        }

        std::optional<frg::small_vector<
            lib::maybe_uspan<std::byte>, 8,
            frg::allocator<lib::maybe_uspan<std::byte>>
        >> read_iov(const msghdr &kmsg)
        {
            frg::small_vector<
                lib::maybe_uspan<std::byte>, 8,
                frg::allocator<lib::maybe_uspan<std::byte>>
            > vec;
            vec.resize(kmsg.msg_iovlen);

            for (std::size_t i = 0; i < kmsg.msg_iovlen; i++)
            {
                iovec local_iov;
                if (!lib::copy_from_user(&local_iov, kmsg.msg_iov + i, sizeof(iovec)))
                    return std::nullopt;

                auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
                if (!uspan.has_value())
                    return std::nullopt;

                vec[i] = *uspan;
            }
            return vec;
        }
    } // namespace

    int socket(int domain, int type, int protocol)
    {
        const auto flags = type & ~0xF;
        if (flags & ~(sock_cloexec | sock_nonblock))
            return -EINVAL;

        if (domain < 0 || domain >= af_max)
            return -EAFNOSUPPORT;

        const auto typ = static_cast<sock_type>(type & 0xF);
        if (!magic_enum::enum_contains(typ))
            return -ESOCKTNOSUPPORT;

        auto sres = socket::create(
            static_cast<addr_fam>(domain),
            typ, protocol
        );
        if (!sres)
            return -lib::map_error(sres.error());

        auto res = socket::create_anon(std::move(*sres), map_flags(flags));
        if (!res)
            return -lib::map_error(res.error());

        return res->first;
    }

    int connect(int sockfd, const sockaddr __user *addr, socklen_t addrlen)
    {
        if (addrlen < sizeof(addr_fam) || addrlen > sizeof(sockaddr_storage))
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        const auto uspan = lib::maybe_uspan<const std::byte>::create(addr, addrlen);
        if (!uspan)
            return -EFAULT;

        if (const auto res = sock->connect(*uspan); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int accept(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen)
    {
        return accept4(sockfd, addr, addrlen, 0);
    }

    std::ssize_t sendto(
        int sockfd, const void __user *buf, std::size_t len,
        std::uint32_t flags, const sockaddr __user *addr, socklen_t addrlen
    )
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        auto bufuspan = lib::maybe_uspan<std::byte>::create(buf, len);
        if (!bufuspan)
            return -EFAULT;

        lib::maybe_uspan<std::byte> nameuspan;
        if (addr)
        {
            auto res = lib::maybe_uspan<std::byte>::create(addr, addrlen);
            if (!res)
                return -EFAULT;

            nameuspan = *res;
        }

        std::span<lib::maybe_uspan<std::byte>> iovs { std::addressof(*bufuspan), 1 };
        const socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = { },
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->sendmsg(hdr, flags);
        if (!res)
            return -lib::map_error(res.error());
        return *res;
    }

    std::ssize_t recvfrom(
        int sockfd, void __user *buf, std::size_t len,
        std::uint32_t flags, sockaddr __user *addr, socklen_t __user *addrlen
    )
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        auto bufuspan = lib::maybe_uspan<std::byte>::create(buf, len);
        if (!bufuspan)
            return -EFAULT;

        socklen_t in_len = 0;
        lib::maybe_uspan<std::byte> nameuspan;
        if (addr && addrlen)
        {
            if (!lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
                return -EFAULT;

            auto res = lib::maybe_uspan<std::byte>::create(addr, in_len);
            if (!res)
                return -EFAULT;

            nameuspan = *res;
        }

        std::span<lib::maybe_uspan<std::byte>> iovs { std::addressof(*bufuspan), 1 };
        const socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = { },
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->recvmsg(hdr, flags);
        if (!res)
            return -lib::map_error(res.error());

        if (addrlen && !lib::copy_to_user(addrlen, &hdr.addr_len_out, sizeof(socklen_t)))
            return -EFAULT;
        return *res;
    }

    std::ssize_t sendmsg(int sockfd, const msghdr __user *msg, std::uint32_t flags)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        msghdr kmsg;
        if (!lib::copy_from_user(&kmsg, msg, sizeof(msghdr)))
            return -EFAULT;

        if (kmsg.msg_iovlen > uio_maxiov)
            return -EMSGSIZE;

        lib::maybe_uspan<std::byte> nameuspan;
        if (kmsg.msg_name)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_name, kmsg.msg_namelen);
            if (!res)
                return -EFAULT;
            nameuspan = *res;
        }

        lib::maybe_uspan<std::byte> ctrluspan;
        if (kmsg.msg_control)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_control, kmsg.msg_controllen);
            if (!res)
                return -EFAULT;
            ctrluspan = *res;
        }

        auto vec = read_iov(kmsg);
        if (!vec)
            return -EFAULT;

        std::span<lib::maybe_uspan<std::byte>> iovs { vec->data(), vec->size() };
        const socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = ctrluspan,
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->sendmsg(hdr, flags);
        if (!res)
            return -lib::map_error(res.error());
        return *res;
    }

    std::ssize_t recvmsg(int sockfd, msghdr __user *msg, std::uint32_t flags)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        msghdr kmsg;
        if (!lib::copy_from_user(&kmsg, msg, sizeof(msghdr)))
            return -EFAULT;

        if (kmsg.msg_iovlen > uio_maxiov)
            return -EMSGSIZE;

        lib::maybe_uspan<std::byte> nameuspan;
        if (kmsg.msg_name)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_name, kmsg.msg_namelen);
            if (!res)
                return -EFAULT;
            nameuspan = *res;
        }

        lib::maybe_uspan<std::byte> ctrluspan;
        if (kmsg.msg_control)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_control, kmsg.msg_controllen);
            if (!res)
                return -EFAULT;
            ctrluspan = *res;
        }

        auto vec = read_iov(kmsg);
        if (!vec)
            return -EFAULT;

        std::span<lib::maybe_uspan<std::byte>> iovs { vec->data(), vec->size() };
        const socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = ctrluspan,
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->recvmsg(hdr, flags);
        if (!res)
            return -lib::map_error(res.error());

        if (!lib::copy_to_user(&msg->msg_namelen, &hdr.addr_len_out, sizeof(socklen_t)))
            return -EFAULT;

        if (!lib::copy_to_user(&msg->msg_controllen, &hdr.msgctrl_len_out, sizeof(socklen_t)))
            return -EFAULT;

        if (!lib::copy_to_user(&msg->msg_flags, &hdr.out_flags, sizeof(int)))
            return -EFAULT;

        return *res;
    }

    int shutdown(int sockfd, int how)
    {
        if (how != shut_rd && how != shut_wr && how != shut_rdwr)
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        if (const auto res = sock->shutdown(how); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int bind(int sockfd, const sockaddr __user *addr, socklen_t addrlen)
    {
        if (addrlen < sizeof(addr_fam) || addrlen > sizeof(sockaddr_storage))
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        const auto uspan = lib::maybe_uspan<const std::byte>::create(addr, addrlen);
        if (!uspan)
            return -EFAULT;

        if (const auto res = sock->bind(*uspan); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int listen(int sockfd, int backlog)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        backlog = std::clamp(backlog, 0, somaxconn);
        if (const auto res = sock->listen(backlog); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int getsockname(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len = 0;
        if (!addr || !addrlen || !lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
            return -EFAULT;

        auto uspan = lib::maybe_uspan<std::byte>::create(addr, in_len);
        if (!uspan)
            return -EFAULT;

        const auto res = sock->getsockname(*uspan);
        if (!res)
            return -lib::map_error(res.error());

        const auto actual_len = *res;
        if (!lib::copy_to_user(addrlen, &actual_len, sizeof(socklen_t)))
            return -EFAULT;

        return 0;
    }

    int getpeername(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len = 0;
        if (!addr || !addrlen || !lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
            return -EFAULT;

        auto uspan = lib::maybe_uspan<std::byte>::create(addr, in_len);
        if (!uspan)
            return -EFAULT;

        const auto res = sock->getpeername(*uspan);
        if (!res)
            return -lib::map_error(res.error());

        const auto actual_len = *res;
        if (!lib::copy_to_user(addrlen, &actual_len, sizeof(socklen_t)))
            return -EFAULT;

        return 0;
    }

    int socketpair(int family, int type, int protocol, int __user *sv /* [2] */)
    {
        const auto flags = type & ~0xF;
        if (flags & ~(sock_cloexec | sock_nonblock))
            return -EINVAL;

        if (family < 0 || family >= af_max)
            return -EAFNOSUPPORT;

        const auto typ = static_cast<sock_type>(type & 0xF);
        if (!magic_enum::enum_contains(typ))
            return -ESOCKTNOSUPPORT;

        const auto proc = sched::current_process();

        auto pres = socket::create_pair(
            static_cast<addr_fam>(family),
            typ, protocol
        );
        if (!pres)
            return -lib::map_error(pres.error());

        auto res1 = socket::create_anon(std::move(pres->first), flags);
        if (!res1)
            return -lib::map_error(res1.error());

        auto res2 = socket::create_anon(std::move(pres->second), flags);
        if (!res2)
        {
            proc->fdt->close(res1->first);
            return -lib::map_error(res2.error());
        }

        int ksv[2] { res1->first, res2->first };
        if (!lib::copy_to_user(sv, ksv, sizeof(int) * 2))
            return -EFAULT;
        return 0;
    }

    int setsockopt(int sockfd, int level, int optname, const char __user *optval, socklen_t optlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        const auto optuspan = lib::maybe_uspan<const std::byte>::create(optval, optlen);
        if (!optuspan)
            return -EFAULT;

        const auto lvl = static_cast<sock_lvl>(level);
        if (const auto res = sock->setsockopt(lvl, optname, *optuspan); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int getsockopt(int sockfd, int level, int optname, char __user *optval, socklen_t __user *optlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len;
        if (!lib::copy_from_user(&in_len, optlen, sizeof(socklen_t)))
            return -EFAULT;

        auto optuspan = lib::maybe_uspan<std::byte>::create(optval, in_len);
        if (!optuspan)
            return -EFAULT;

        const auto lvl = static_cast<sock_lvl>(level);
        const auto res = sock->getsockopt(lvl, optname, *optuspan);
        if (!res)
            return -lib::map_error(res.error());

        const auto actual_len = *res;
        if (!lib::copy_to_user(optlen, &actual_len, sizeof(socklen_t)))
            return -EFAULT;
        return 0;
    }

    int accept4(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen, int flags)
    {
        if (flags & ~(sock_cloexec | sock_nonblock))
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len = 0;
        lib::maybe_uspan<std::byte> uspan;
        if (addr && addrlen)
        {
            if (!lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
                return -EFAULT;

            auto res = lib::maybe_uspan<std::byte>::create(addr, in_len);
            if (!res)
                return -EFAULT;

            uspan = *res;
        }

        socklen_t out_len = in_len;
        auto ares = sock->accept(uspan, &out_len, flags);
        if (!ares)
            return -lib::map_error(ares.error());

        auto res = socket::create_anon(std::move(*ares), map_flags(flags));
        if (!res)
            return -lib::map_error(res.error());

        if (addr && addrlen)
        {
            if (!lib::copy_to_user(addrlen, &out_len, sizeof(socklen_t)))
                return -EFAULT;
        }

        return res->first;
    }

    int getdents64(int fd, dirent64 __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        auto &dentry = fdesc->file->path.dentry;
        auto &inode = dentry->inode;
        auto &stat = inode->stat;

        if (stat.type() != stat::type::s_ifdir)
            return -ENOTDIR;

        const auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = fdesc->file->getdents(*uspan);
        if (!ret.has_value())
            return -lib::map_error(ret.error());

        if (const auto err = touch_atime(fdesc->file); err < 0)
            return err;

        return static_cast<int>(ret.value());
    }

    int fadvise64(int fd, loff_t offset, std::size_t len, int advice)
    {
        lib::unused(fd, offset, len, advice);
        return -ENOSYS;
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
                    return -EINVAL;

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

                thread->state.store(sched::thread_state::sleeping, std::memory_order_release);
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
                    return -EINTR;
                }
            }
        }

        int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, timespec *timeout, bool update_timeout, const sigset_t *sigmask)
        {
            if (nfds < 0 || nfds > FD_SETSIZE)
                return -EINVAL;

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
                    return -EFAULT;
            if (writefds && !lib::copy_from_user(&kwritefds, writefds, sizeof(fd_set)))
                    return -EFAULT;
            if (exceptfds && !lib::copy_from_user(&kexceptfds, exceptfds, sizeof(fd_set)))
                    return -EFAULT;
            if (sigmask && !lib::copy_from_user(&ksigmask, sigmask, sizeof(sigset_t)))
                    return -EFAULT;

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
                    return -EFAULT;
                if (writefds && !lib::copy_to_user(writefds, &kwritefds, sizeof(fd_set)))
                        return -EFAULT;
                if (exceptfds && !lib::copy_to_user(exceptfds, &kexceptfds, sizeof(fd_set)))
                        return -EFAULT;
            }

            return ret;
        }
    } // namespace

    int ppoll(
        pollfd __user *fds, nfds_t nfds, timespec __user *timeout,
        sigset_t __user *sigmask
    )
    {
        if (fds == nullptr)
            return -EFAULT;

        std::vector<pollfd> kfds;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto &kfd = kfds.emplace_back();
            if (!lib::copy_from_user(&kfd, &fds[i], sizeof(pollfd)))
                return -EFAULT;
        }

        timespec ktimeout;
        if (timeout != nullptr)
        {
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return -EFAULT;
        }

        sigset_t ksigmask;
        if (sigmask != nullptr)
        {
            if (!lib::copy_from_user(&ksigmask, sigmask, sizeof(sigset_t)))
                return -EFAULT;
        }

        const auto ret = ppoll(
            kfds,
            timeout ? &ktimeout : nullptr,
            sigmask ? &ksigmask : nullptr
        );

        for (nfds_t i = 0; i < nfds; i++)
        {
            if (!lib::copy_to_user(&fds[i], &kfds[i], sizeof(pollfd)))
                return -EFAULT;
        }

        if (ret >= 0 && timeout != nullptr)
        {
            if (!lib::copy_to_user(timeout, &ktimeout, sizeof(timespec)))
                return -EFAULT;
        }

        return ret;
    }

    int poll(pollfd __user *fds, nfds_t nfds, int timeout)
    {
        if (fds == nullptr)
            return -EFAULT;

        std::vector<pollfd> kfds;
        for (nfds_t i = 0; i < nfds; i++)
        {
            auto &kfd = kfds.emplace_back();
            if (!lib::copy_from_user(&kfd, &fds[i], sizeof(pollfd)))
                return -EFAULT;
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
                return -EFAULT;
        }

        return ret;
    }

    int select(
        int nfds, fd_set __user *readfds, fd_set __user *writefds,
        fd_set __user *exceptfds, timeval __user *timeout
    )
    {
        timespec ktimeout;
        if (timeout != nullptr)
        {
            timeval ktimeval;
            if (!lib::copy_from_user(&ktimeval, timeout, sizeof(timeval)))
                return -EFAULT;
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
                return -EFAULT;
        }

        return ret;
    }

    int pselect6(
        int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds,
        const timespec __user *timeout, const struct sigset_t __user *sigmask
    )
    {
        timespec ktimeout;
        if (timeout != nullptr)
        {
            if (!lib::copy_from_user(&ktimeout, timeout, sizeof(timespec)))
                return -EFAULT;
        }

        return pselect(
            nfds, readfds, writefds, exceptfds,
            timeout ? &ktimeout : nullptr,
            false, sigmask
        );
    }

    namespace
    {
        lib::expect<path> xattr_target_path(
            sched::process_t *proc,
            const char __user *pathname, bool follow_links
        )
        {
            return get_target(proc, at_fdcwd, pathname, follow_links, false, true);
        }

        lib::expect<path> xattr_target_fd(sched::process_t *proc, int fd)
        {
            auto fdesc = get_fd(proc, fd);
            if (!fdesc)
                return std::unexpected { fdesc.error() };
            return (*fdesc)->file->path;
        }

        lib::expect<std::string> xattr_name(const char __user *name)
        {
            if (name == nullptr)
                return std::unexpected { lib::err::invalid_address };
            auto str = lib::user_string::get(name, xattr_name_max + 1);
            if (!str.has_value())
                return std::unexpected { lib::err::invalid_path };
            return std::move(*str);
        }

        int do_setxattr(
            const path &target, std::string_view name,
            const void __user *value, std::size_t size, int flags
        )
        {
            if (size > xattr_size_max)
                return -E2BIG;
            if (readonly_mount(target))
                return -EROFS;

            auto uspan = lib::maybe_uspan<std::byte>::create(
                const_cast<void __user *>(value), size
            );
            if (!uspan.has_value())
                return -EFAULT;

            if (const auto ret = setxattr(target, name, *uspan, flags); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }

        std::ssize_t do_getxattr(
            const path &target, std::string_view name,
            void __user *value, std::size_t size
        )
        {
            auto ret = getxattr(target, name);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            const auto buf_size = ret->size();
            if (size == 0)
                return static_cast<std::ssize_t>(buf_size);
            if (size < buf_size)
                return -ERANGE;

            if (buf_size > 0 && !lib::copy_to_user(value, ret->data(), buf_size))
                return -EFAULT;
            return static_cast<std::ssize_t>(buf_size);
        }

        std::ssize_t do_listxattr(const path &target, char __user *list, std::size_t size)
        {
            if (size == 0)
            {
                auto ret = lenxattrs(target);
                if (!ret.has_value())
                    return -lib::map_error(ret.error());
                return static_cast<std::ssize_t>(*ret);
            }

            auto ret = listxattrs(target);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            std::size_t total = 0;
            for (const auto &name : *ret)
                total += name.size() + 1;

            if (size < total)
                return -ERANGE;

            std::vector<char> buf;
            buf.reserve(total);
            for (const auto &name : *ret)
            {
                buf.insert(buf.end(), name.begin(), name.end());
                buf.push_back('\0');
            }

            if (total > 0 && !lib::copy_to_user(list, buf.data(), total))
                return -EFAULT;
            return static_cast<std::ssize_t>(total);
        }

        int do_removexattr(const path &target, std::string_view name)
        {
            if (readonly_mount(target))
                return -EROFS;
            if (const auto ret = remxattr(target, name); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }
    } // namespace

    int setxattr(
        const char __user *pathname, const char __user *name,
        const void __user *value, std::size_t size, int flags
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_setxattr(*target, *kname, value, size, flags);
    }

    int lsetxattr(
        const char __user *pathname, const char __user *name,
        const void __user *value, std::size_t size, int flags
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_setxattr(*target, *kname, value, size, flags);
    }

    int fsetxattr(int fd, const char __user *name, const void __user *value, std::size_t size, int flags)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_setxattr(*target, *kname, value, size, flags);
    }

    std::ssize_t getxattr(
        const char __user *pathname, const char __user *name,
        void __user *value, std::size_t size
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_getxattr(*target, *kname, value, size);
    }

    std::ssize_t lgetxattr(
        const char __user *pathname, const char __user *name,
        void __user *value, std::size_t size
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_getxattr(*target, *kname, value, size);
    }

    std::ssize_t fgetxattr(int fd, const char __user *name, void __user *value, std::size_t size)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_getxattr(*target, *kname, value, size);
    }

    std::ssize_t listxattr(const char __user *pathname, char __user *list, std::size_t size)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        return do_listxattr(*target, list, size);
    }

    std::ssize_t llistxattr(const char __user *pathname, char __user *list, std::size_t size)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        return do_listxattr(*target, list, size);
    }

    std::ssize_t flistxattr(int fd, char __user *list, std::size_t size)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        return do_listxattr(*target, list, size);
    }

    int removexattr(const char __user *pathname, const char __user *name)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_removexattr(*target, *kname);
    }

    int lremovexattr(const char __user *pathname, const char __user *name)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_removexattr(*target, *kname);
    }

    int fremovexattr(int fd, const char __user *name)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_removexattr(*target, *kname);
    }

    int fsopen(const char __user *fsname, std::uint32_t flags)
    {
        // TODO
        lib::unused(fsname, flags);
        return -ENOSYS;
    }

    int inotify_init1(int flags)
    {
        // TODO
        lib::unused(flags);
        return -ENOSYS;
    }
} // namespace syscall::vfs
