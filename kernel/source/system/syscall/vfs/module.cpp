// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.bin.elf;

namespace syscall::vfs
{
    int init_module(void __user *umod, unsigned long len, const char __user *uargs)
    {
        // TODO
        lib::unused(uargs);

        const auto proc = sched::current_process();
        if (!sched::capable(proc->cred, sched::cap_t::sys_module))
            return -EPERM;

        const auto uspan = lib::maybe_uspan<std::byte>::create(umod, len);
        if (!uspan)
            return -EFAULT;

        lib::membuffer buffer { len };
        if (!uspan->copy_to(buffer.span()))
            return -EFAULT;

        if (const auto ret = bin::elf::mod::load(std::move(buffer), true); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int finit_module(int fd, const char __user *uargs, int flags)
    {
        // TODO
        lib::unused(uargs, flags);

        const auto proc = sched::current_process();
        if (!sched::capable(proc->cred, sched::cap_t::sys_module))
            return -EPERM;

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

        if (stat.type() != stat::type::s_ifreg)
            return -EISDIR;

        lib::membuffer buffer { static_cast<std::size_t>(stat.st_size) };

        auto uspan = lib::maybe_uspan<std::byte>::create(buffer.data(), buffer.size());
        lib::bug_on(!uspan.has_value());

        const auto ret = fdesc->file->read(*uspan);
        if (!ret.has_value())
            return -lib::map_error(ret.error());

        if (const auto ret = bin::elf::mod::load(std::move(buffer), true); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }
} // namespace syscall::vfs
