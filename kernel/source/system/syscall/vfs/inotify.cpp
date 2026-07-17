// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    int inotify_init1(int flags)
    {
        // TODO
        lib::unused(flags);
        return -ENOSYS;
    }

    int inotify_init()
    {
        return inotify_init1(0);
    }

    int inotify_add_watch(int fd, const char __user *pathname, std::uint32_t mask)
    {
        // TODO
        lib::unused(fd, pathname, mask);
        return -ENOSYS;
    }

    int inotify_rm_watch(int fd, std::int32_t wd)
    {
        // TODO
        lib::unused(fd, wd);
        return -ENOSYS;
    }
} // namespace syscall::vfs
