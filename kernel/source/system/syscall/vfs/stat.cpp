// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.vfs.dev;

namespace syscall::vfs
{
    using namespace ::vfs;

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
        auto fdesc = detail::get_fd(proc, fd);
        if (!fdesc)
            return -lib::map_error(fdesc.error());
        if (!(*fdesc)->file)
            return -EBADF;
        return do_statfs((*fdesc)->file->path, buf);
    }

    int getdents64(int fd, dirent64 __user *buf, std::size_t count)
    {
        const auto proc = sched::current_process();

        const auto fdesc_res = detail::get_fd(proc, fd);
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

        if (const auto err = detail::touch_atime(fdesc->file); err < 0)
            return err;

        return static_cast<int>(ret.value());
    }
} // namespace syscall::vfs
