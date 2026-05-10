// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.vfs.pipe;

namespace syscall::vfs
{
    using namespace ::vfs;

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

        const auto max_fd = proc->rlimits->get(sched::rlimit_nofile).cur;

        const auto fd0res = fdt->alloc(rfdesc, 0, false, max_fd);
        if (!fd0res.has_value())
            return -lib::map_error(fd0res.error());
        fds[0] = *fd0res;

        if (const auto ret = rfdesc->file->open(flags | o_rdonly, proc->pid); !ret)
        {
            detail::close_fd(proc, fds[0], false);
            return -lib::map_error(ret.error());
        }

        const auto wdentry = std::make_shared<dentry>();
        wdentry->name = "<[PIPE WRITE]>";
        wdentry->inode = std::move(shared_inode);

        const auto wfdesc = filedesc::create({
            .dentry = wdentry,
            .mnt = nullptr
        }, flags | o_wronly);

        const auto fd1res = fdt->alloc(wfdesc, 0, false, max_fd);
        if (!fd1res.has_value())
        {
            detail::close_fd(proc, fds[0]);
            return -lib::map_error(fd1res.error());
        }
        fds[1] = *fd1res;

        if (const auto ret = wfdesc->file->open(flags | o_wronly, proc->pid); !ret)
        {
            detail::close_fd(proc, fds[0]);
            detail::close_fd(proc, fds[1], false);
            return -lib::map_error(ret.error());
        }

        if (!lib::copy_to_user(pipefd, fds.data(), sizeof(int) * 2))
        {
            detail::close_fd(proc, fds[0]);
            detail::close_fd(proc, fds[1]);
            return -EFAULT;
        }
        return 0;
    }

    int pipe(int __user *pipefd)
    {
        return pipe2(pipefd, 0);
    }

    int inotify_init1(int flags)
    {
        // TODO
        lib::unused(flags);
        return -ENOSYS;
    }
} // namespace syscall::vfs
