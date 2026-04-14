// Copyright (C) 2024-2025  ilobilo

module system.syscall.memory;

import system.memory.virt;
import system.scheduler;
import lib;

import cppstd;

namespace syscall::memory
{
    void *mmap(void *addr, std::size_t length, int prot, int flags, int fd, off_t offset)
    {
        static void *invalid_addr = reinterpret_cast<void *>(-1);

        const bool priv = (flags & vmm::flag::private_);
        const bool shared = (flags & vmm::flag::shared);
        const bool fixed = (flags & vmm::flag::fixed);
        const bool anon = (flags & vmm::flag::anonymous);

        if ((priv && shared) || (!priv && !shared) || (fd >= 0 && anon) || length == 0)
            return (errno = EINVAL, invalid_addr);

        const auto psize = vmm::default_page_size();
        if (length % psize != 0 || offset % psize != 0)
            return (errno = EINVAL, invalid_addr);

        if (anon && fd != -1)
            return (errno = EINVAL, invalid_addr);

        if (!anon && fd < 0)
            return (errno = EBADF, invalid_addr);

        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);
        const auto &vmspace = proc->vmspace;

        std::uintptr_t address = reinterpret_cast<std::uintptr_t>(addr);

        if (fixed == true)
        {
            if (address == 0 || address % psize != 0)
                return (errno = EINVAL, invalid_addr);

            if (vmspace->is_mapped(address, length) && !vmspace->unmap(address, length))
                return (errno = EINVAL, invalid_addr);
        }
        else
        {
            address = vmspace->find_free_region(length);
            if (address == 0)
                return (errno = ENOMEM, invalid_addr);
        }

        if (address + length < address)
            return (errno = EINVAL, invalid_addr);

        std::shared_ptr<vmm::object> obj;
        if (!anon && fd >= 0)
        {
            auto fdesc = proc->fdt.get(static_cast<std::size_t>(fd));
            if (!fdesc)
                return (errno = EBADF, invalid_addr);

            obj = fdesc->file->path.dentry->inode->map(priv);
            if (!obj)
                return (errno = ENODEV, invalid_addr);
        }
        else
        {
            if (offset != 0)
                return (errno = EINVAL, invalid_addr);
            obj = std::make_shared<vmm::memobject>();
        }

        if (!vmspace->map(
            address, length,
            static_cast<std::uint8_t>(prot),
            static_cast<std::uint8_t>(flags),
            obj, static_cast<off_t>(offset)
        ))
            return (errno = ENOMEM, invalid_addr);

        return reinterpret_cast<void *>(address);
    }

    int munmap(void *addr, std::size_t length)
    {
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->unmap(reinterpret_cast<std::uintptr_t>(addr), length);
        return res ? 0 : (errno = EINVAL, -1);
    }

    int mprotect(void *addr, std::size_t len, int prot)
    {
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->protect(
            reinterpret_cast<std::uintptr_t>(addr), len,
            static_cast<std::uint8_t>(prot)
        );

        return res ? 0 : (errno = ENOMEM, -1);
    }
} // namespace syscall::memory