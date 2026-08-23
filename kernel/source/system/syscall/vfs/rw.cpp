// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        rlim_t fsize_limit(const std::shared_ptr<file_t> &file)
        {
            if (file->path.dentry->inode->stat.type() != stat::type::s_ifreg)
                return rlim_inf;
            return sched::current_process()->rlimits->get(sched::rlimit_fsize).cur;
        }

        bool exceeds_fsize(rlim_t limit, rlim_t offset, std::size_t count)
        {
            if (limit == rlim_inf || count == 0)
                return false;
            return offset >= limit || count > limit - offset;
        }

        lib::expect<std::shared_ptr<file_t>> rw_file(int fd, bool write)
        {
            auto *const proc = sched::current_process();

            const auto fdesc_res = detail::get_fd(proc, fd);
            if (!fdesc_res)
                return std::unexpected { fdesc_res.error() };

            const auto &file = (*fdesc_res)->file;
            if (write ? !is_write(file->flags) : !is_read(file->flags))
                return std::unexpected { lib::err::invalid_fd };

            if (write && detail::readonly_mount(file->path))
                return std::unexpected { lib::err::read_only_fs };

            if (file->path.dentry->inode->stat.type() == stat::type::s_ifdir)
                return std::unexpected { lib::err::target_is_a_dir };

            return file;
        }

        int rw_finish(const std::shared_ptr<file_t> &file, bool write)
        {
            const auto &dentry = file->path.dentry;
            if (dentry && dentry->inode)
            {
                if (write)
                {
                    auto &inode = dentry->inode;
                    const std::unique_lock _ { inode->lock };
                    inode->stat.update_time(kstat::time::modify | kstat::time::status);

                    if (const auto ret = dirty_inode(file->path); !ret)
                        return -lib::map_error(ret.error());
                }
                else if (const auto err = detail::touch_atime(file); err < 0)
                    return err;
            }

            if (file->flags & (o_sync | o_dsync))
            {
                if (const auto ret = file->sync(); !ret)
                    return -lib::map_error(ret.error());
            }
            return 0;
        }

        lib::expect<std::size_t> transfer(
            const std::shared_ptr<file_t> &file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer, bool write, bool pos
        )
        {
            if (write)
                return pos ? file->pwrite(offset, buffer) : file->write(buffer);
            return pos ? file->pread(offset, buffer) : file->read(buffer);
        }

        std::ssize_t do_rw(
            int fd, const void __user *buf, std::size_t count,
            off_t offset, bool write, bool pos
        )
        {
            if (pos && offset < 0)
                return -EINVAL;

            const auto file_res = rw_file(fd, write);
            if (!file_res)
                return -lib::map_error(file_res.error());
            const auto &file = *file_res;

            const off_t off = pos ? offset : file->offset;
            if (write && exceeds_fsize(fsize_limit(file), off, count))
                return -EFBIG;

            const auto uspan = lib::maybe_uspan<std::byte>::create(buf, count);
            if (!uspan.has_value())
                return -EFAULT;

            const auto ret = transfer(file, off, *uspan, write, pos);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            if (const auto err = rw_finish(file, write); err < 0)
                return err;

            return *ret;
        }

        std::ssize_t do_rw_iovec(
            int fd, const iovec __user *iov, int iovcnt,
            off_t offset, bool write, bool pos
        )
        {
            if (pos && offset < 0)
                return -EINVAL;

            const auto file_res = rw_file(fd, write);
            if (!file_res)
                return -lib::map_error(file_res.error());
            const auto &file = *file_res;

            if (iovcnt < 0 || static_cast<std::size_t>(iovcnt) > uio_maxiov)
                return -EINVAL;

            constexpr std::size_t count_max = std::numeric_limits<std::ssize_t>::max();
            const auto limit = write ? fsize_limit(file) : rlim_inf;

            std::size_t total = 0;
            std::ssize_t err = 0;

            for (int i = 0; i < iovcnt; i++)
            {
                iovec local_iov;
                if (!lib::copy_from_user(&local_iov, iov + i, sizeof(iovec)))
                {
                    err = -EFAULT;
                    break;
                }

                if (local_iov.iov_len > count_max - total)
                {
                    err = -EINVAL;
                    break;
                }

                const off_t off = pos ? offset + total : file->offset;
                if (write && exceeds_fsize(limit, off, local_iov.iov_len))
                {
                    err = -EFBIG;
                    break;
                }

                const auto uspan = lib::maybe_uspan<std::byte>::create(
                    local_iov.iov_base, local_iov.iov_len
                );
                if (!uspan.has_value())
                {
                    err = -EFAULT;
                    break;
                }

                const auto ret = transfer(file, off, *uspan, write, pos);
                if (!ret.has_value())
                {
                    err = -lib::map_error(ret.error());
                    break;
                }

                total += *ret;
                if (*ret != local_iov.iov_len)
                    break;
            }

            if (total == 0 && err < 0)
                return err;

            if (const auto ret = rw_finish(file, write); ret < 0)
                return ret;

            return total;
        }
    } // namespace

    std::ssize_t read(int fd, void __user *buf, std::size_t count)
    {
        return do_rw(fd, buf, count, 0, false, false);
    }

    std::ssize_t write(int fd, const void __user *buf, std::size_t count)
    {
        return do_rw(fd, buf, count, 0, true, false);
    }

    std::ssize_t pread(int fd, void __user *buf, std::size_t count, off_t offset)
    {
        return do_rw(fd, buf, count, offset, false, true);
    }

    std::ssize_t pwrite(int fd, const void __user *buf, std::size_t count, off_t offset)
    {
        return do_rw(fd, buf, count, offset, true, true);
    }

    std::ssize_t readv(int fd, const iovec __user *iov, int iovcnt)
    {
        return do_rw_iovec(fd, iov, iovcnt, 0, false, false);
    }

    std::ssize_t writev(int fd, const iovec __user *iov, int iovcnt)
    {
        return do_rw_iovec(fd, iov, iovcnt, 0, true, false);
    }

    std::ssize_t preadv(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        return do_rw_iovec(fd, iov, iovcnt, offset, false, true);
    }

    std::ssize_t pwritev(int fd, const iovec __user *iov, int iovcnt, off_t offset)
    {
        return do_rw_iovec(fd, iov, iovcnt, offset, true, true);
    }

    // std::ssize_t preadv2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);
    // std::ssize_t pwritev2(int fd, const iovec __user *iov, int iovcnt, off_t offset, int flags);

    // not really the correct implementation but it's good enough
    std::ssize_t splice(
        int fd_in, off_t __user *off_in,
        int fd_out, off_t __user *off_out,
        std::size_t len, std::uint32_t flags
    )
    {
        constexpr std::size_t chunk_max = 64uz * 1024;

        constexpr std::uint32_t splice_f_move = 1;
        constexpr std::uint32_t splice_f_nonblock = 2;
        constexpr std::uint32_t splice_f_more = 4;
        constexpr std::uint32_t splice_f_gift = 8;

        if (flags & ~(splice_f_move | splice_f_nonblock | splice_f_more | splice_f_gift))
            return -EINVAL;

        auto *const proc = sched::current_process();

        const auto in_res = detail::get_fd(proc, fd_in);
        if (!in_res)
            return -lib::map_error(in_res.error());
        const auto out_res = detail::get_fd(proc, fd_out);
        if (!out_res)
            return -lib::map_error(out_res.error());

        const auto &in = (*in_res)->file;
        const auto &out = (*out_res)->file;

        if (!is_read(in->flags) || !is_write(out->flags))
            return -EBADF;

        const auto type_of = [](const std::shared_ptr<file_t> &file) {
            const auto &dentry = file->path.dentry;
            if (!dentry || !dentry->inode)
                return stat::type::s_ifsock;
            return dentry->inode->stat.type();
        };

        const bool in_pipe = type_of(in) == stat::type::s_ififo;
        const bool out_pipe = type_of(out) == stat::type::s_ififo;

        if (!in_pipe && !out_pipe)
            return -EINVAL;
        if ((in_pipe && off_in) || (out_pipe && off_out))
            return -ESPIPE;

        if (type_of(out) == stat::type::s_ifdir)
            return -EISDIR;
        if (detail::readonly_mount(out->path))
            return -EROFS;

        off_t in_offset = 0, out_offset = 0;
        if (off_in && !lib::copy_from_user(&in_offset, off_in, sizeof(in_offset)))
            return -EFAULT;
        if (off_out && !lib::copy_from_user(&out_offset, off_out, sizeof(out_offset)))
            return -EFAULT;
        if (in_offset < 0 || out_offset < 0)
            return -EINVAL;

        if (len == 0)
            return 0;

        if (flags & splice_f_nonblock)
        {
            if (in_pipe)
            {
                const auto ev = in->poll(nullptr);
                if (ev.has_value() && !(*ev & (pollin | pollhup)))
                    return -EAGAIN;
            }

            if (out_pipe)
            {
                const auto ev = out->poll(nullptr);
                if (ev.has_value() && !(*ev & pollout))
                    return -EAGAIN;
            }
        }

        lib::membuffer buffer { std::min(len, chunk_max) };
        const auto buffer_uspan = buffer.uspan();
        if (!buffer_uspan.has_value())
            return -ENOMEM;

        std::size_t total = 0;
        while (total < len)
        {
            const auto to_read = std::min(len - total, buffer.size());
            auto rspan = buffer_uspan->subspan(0, to_read);

            const auto rret = off_in
                ? in->pread(in_offset, rspan)
                : in->read(rspan);
            if (!rret.has_value())
            {
                if (total > 0)
                    break;
                return -lib::map_error(rret.error());
            }

            const auto nread = *rret;
            if (nread == 0)
                break;

            std::size_t written = 0;
            while (written < nread)
            {
                auto wspan = buffer_uspan->subspan(written, nread - written);

                const auto wret = off_out
                    ? out->pwrite(out_offset, wspan)
                    : out->write(wspan);
                if (!wret.has_value() || *wret == 0)
                {
                    total += written;
                    if (total > 0)
                        goto done;
                    return wret.has_value() ? -EIO : -lib::map_error(wret.error());
                }

                written += *wret;
                if (off_out)
                    out_offset += *wret;
            }

            total += nread;
            if (off_in)
                in_offset += nread;

            if (nread < to_read)
                break;

            if (in_pipe)
                break;
        }

        done:
        if (const auto ret = rw_finish(in, false); ret < 0 && total == 0)
            return ret;
        if (const auto ret = rw_finish(out, true); ret < 0 && total == 0)
            return ret;

        if (off_in && !lib::copy_to_user(off_in, &in_offset, sizeof(in_offset)))
            return -EFAULT;
        if (off_out && !lib::copy_to_user(off_out, &out_offset, sizeof(out_offset)))
            return -EFAULT;

        return total;
    }

    off_t lseek(int fd, off_t offset, int whence)
    {
        auto *const proc = sched::current_process();

        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        const auto &file = fdesc->file;
        const auto &stat = file->path.dentry->inode->stat;
        const auto type = stat.type();
        if (type == stat::s_ifsock || type == stat::s_ififo)
            return -ESPIPE;

        if (const auto &ops = file->ops; !ops || !ops->seekable())
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
        int do_trunc(const path_t &target, off_t length)
        {
            auto &inode = target.dentry->inode;
            if (inode->stat.type() == stat::type::s_ifdir)
                return -EISDIR;
            if (inode->stat.type() != stat::type::s_ifreg)
                return -EINVAL;

            if (detail::readonly_mount(target))
                return -EROFS;

            auto file = vfs::file_t::create(target, 0, o_wronly);
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

        auto *const proc = sched::current_process();
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

        auto *const proc = sched::current_process();
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

    int fallocate(int fd, int mode, off_t offset, off_t len)
    {
        // TODO: other modes
        constexpr auto fl_keep_size = 0x01;

        if (offset < 0 || len <= 0)
            return -EINVAL;

        if (mode & ~fl_keep_size)
        {
            lib::error("fallocate: unsupported mode 0x{:X}", mode);
            return -EOPNOTSUPP;
        }

        auto *const proc = sched::current_process();
        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());
        const auto &fdesc = *fdesc_res;

        if (!is_write(fdesc->file->flags))
            return -EBADF;

        const auto &path = fdesc->file->path;
        auto &inode = path.dentry->inode;
        if (inode->stat.type() == stat::type::s_ifdir)
            return -EISDIR;
        if (inode->stat.type() != stat::type::s_ifreg)
            return -ENODEV;

        if (detail::readonly_mount(path))
            return -EROFS;

        const auto end = static_cast<std::uint64_t>(offset) + len;
        if (!(mode & fl_keep_size) && end > static_cast<std::uint64_t>(inode->stat.st_size))
        {
            if (end > proc->rlimits->get(sched::rlimit_fsize).cur)
                return -EFBIG;
            return do_trunc(path, end);
        }
        return 0;
    }

    int fadvise64(int fd, loff_t offset, std::size_t len, int advice)
    {
        // TODO
        if (advice < 0 || advice > 5)
            return -EINVAL;

        if (offset < 0)
            return -EINVAL;

        auto *const proc = sched::current_process();
        const auto fdesc_res = detail::get_fd(proc, fd);
        if (!fdesc_res)
            return -lib::map_error(fdesc_res.error());

        lib::unused(len);
        return 0;
    }
} // namespace syscall::vfs
