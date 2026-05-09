// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = detail::get_fd(proc, fd);
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

        if (const auto err = detail::touch_atime(file); err < 0)
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

        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (detail::readonly_mount(file->path))
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

        const auto fdesc_res = detail::get_fd(proc, fd);
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

        if (const auto err = detail::touch_atime(file); err < 0)
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

        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (detail::readonly_mount(file->path))
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

        const auto fdesc_res = detail::get_fd(proc, fd);
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

        if (const auto err = detail::touch_atime(file); err < 0)
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

        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (detail::readonly_mount(file->path))
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

        const auto fdesc_res = detail::get_fd(proc, fd);
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

        if (const auto err = detail::touch_atime(file); err < 0)
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

        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        if (!is_write(file->flags))
            return -EBADF;

        if (detail::readonly_mount(file->path))
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

        const auto fdesc_res = detail::get_fd(proc, fd);
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

    namespace
    {
        int do_trunc(const path &target, off_t length)
        {
            auto &inode = target.dentry->inode;
            if (inode->stat.type() == stat::type::s_ifdir)
                return -EISDIR;
            if (inode->stat.type() != stat::type::s_ifreg)
                return -EINVAL;

            if (detail::readonly_mount(target))
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

        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        if (!is_write(fdesc->file->flags))
            return -EINVAL;

        return do_trunc(fdesc->file->path, length);
    }

    int fadvise64(int fd, loff_t offset, std::size_t len, int advice)
    {
        lib::unused(fd, offset, len, advice);
        return -ENOSYS;
    }
} // namespace syscall::vfs
