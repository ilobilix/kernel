// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.vfs.pipe;

namespace syscall::vfs
{
    using namespace ::vfs;

    int ioctl(int fd, std::uint64_t request, void __user *argp)
    {
        constexpr std::uint64_t fionbio = 0x5421;
        constexpr std::uint64_t fioclex = 0x5451;
        constexpr std::uint64_t fionclex = 0x5450;

        const auto proc = sched::current_process();

        const auto fdesc_res = detail::get_fd(proc, fd);
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

        const auto fdesc_res = detail::get_fd(proc, fd);
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
                return fdesc->closexec ? 1 : 0;
            case 2: // F_SETFD
                fdesc->closexec = (arg & 1) != 0; // FD_CLOEXEC
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
                return *ret;
            }
            case 1032: // F_GETPIPE_SZ
            {
                const auto ret = vfs::pipe::get_size(fdesc->file);
                if (!ret)
                    return -lib::map_error(ret.error());
                return *ret;
            }
            // TODO: actually enforce locks
            case 5: // F_GETLK
            case 6: // F_SETLK
            case 7: // F_SETLKW
            case 36: // F_OFD_GETLK
            case 37: // F_OFD_SETLK
            case 38: // F_OFD_SETLKW
            {
                struct flock
                {
                    std::int16_t l_type;
                    std::int16_t l_whence;
                    off_t l_start;
                    off_t l_len;
                    pid_t l_pid;
                };

                const bool ofd = (cmd >= 0x24);
                const bool getlk = (cmd == 5 || cmd == 0x24);

                flock fl;
                if (!lib::copy_from_user(&fl, reinterpret_cast<const flock __user *>(arg), sizeof(fl)))
                    return -EFAULT;

                // F_RDLCK, F_WRLCK, F_UNLCK
                if (fl.l_type != 0 && fl.l_type != 1 && fl.l_type != 2)
                    return -EINVAL;

                if (fl.l_whence != seek_set && fl.l_whence != seek_cur && fl.l_whence != seek_end)
                    return -EINVAL;

                if (ofd && fl.l_pid != 0)
                    return -EINVAL;

                if (getlk)
                {
                    fl.l_type = 2;
                    if (!lib::copy_to_user(reinterpret_cast<flock __user *>(arg), &fl, sizeof(fl)))
                        return -EFAULT;
                }
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
        const auto fdesc = detail::get_fd(proc, fd);
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
        if (oldfd == newfd || (flags & ~o_cloexec) != 0)
            return -EINVAL;

        const auto proc = sched::current_process();
        const auto fdres = proc->fdt->dup(
            oldfd, newfd, (flags & o_cloexec) != 0, true,
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
} // namespace syscall::vfs
