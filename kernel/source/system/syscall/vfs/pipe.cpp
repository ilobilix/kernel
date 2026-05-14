// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.vfs.pipe;

namespace syscall::vfs
{
    using namespace ::vfs;

    int pipe2(int __user *pipefd, int flags)
    {
        const auto proc = sched::current_process();

        if (flags & ~(o_cloexec | o_nonblock))
            return -EINVAL;

        const auto ret = pipe::create_pair(flags);
        if (!ret)
            return -lib::map_error(ret.error());

        std::array<int, 2> fds { ret->first, ret->second };
        if (!lib::copy_to_user(pipefd, fds.data(), sizeof(int) * 2))
        {
            proc->fdt->close(fds[0]);
            proc->fdt->close(fds[1]);
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
