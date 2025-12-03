// Copyright (C) 2024-2025  ilobilo

module system.syscall.memory;

import system.memory.virt;
import system.scheduler;
import lib;

import std;

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
        if (offset % psize != 0)
            return (errno = EINVAL, invalid_addr);

        length = lib::align_up(length, psize);

        if (anon && fd != -1)
            return (errno = EINVAL, invalid_addr);

        if (!anon && fd < 0)
            return (errno = EBADF, invalid_addr);

        const auto proc = sched::this_thread()->parent;
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
            auto ret = vmspace->find_free_region(length);
            if (!ret.has_value())
                return (errno = ENOMEM, invalid_addr);
            address = ret.value();
        }

        if (address + length < address)
            return (errno = EINVAL, invalid_addr);

        std::shared_ptr<vmm::object> obj;
        if (!anon && fd >= 0)
        {
            auto fdesc = proc->fdt.get(static_cast<std::size_t>(fd));
            if (!fdesc)
                return (errno = EBADF, invalid_addr);

            obj = fdesc->file->map(priv);
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
        const auto proc = sched::this_thread()->parent;
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->unmap(reinterpret_cast<std::uintptr_t>(addr), length);
        return res ? 0 : (errno = EINVAL, -1);
    }

    int mprotect(void *addr, std::size_t len, int prot)
    {
        const auto proc = sched::this_thread()->parent;
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->protect(
            reinterpret_cast<std::uintptr_t>(addr), len,
            static_cast<std::uint8_t>(prot)
        );

        return res ? 0 : (errno = ENOMEM, -1);
    }

    void *brk(void *addr)
    {
        const auto proc = sched::this_thread()->parent;
        const auto &vmspace = proc->vmspace;

        const auto psize = vmm::default_page_size();

        const auto address = reinterpret_cast<std::uintptr_t>(addr);
        const auto old = vmspace->brk;

        if (address == old)
            return reinterpret_cast<void *>(old);

        if (address > old)
        {
            const auto begin = lib::align_up(old, psize);
            const auto length = lib::align_up(address - begin, psize);
            if (length == 0 || vmspace->is_mapped(begin, length))
                return reinterpret_cast<void *>(old);

            auto obj = std::make_shared<vmm::memobject>();
            if (!vmspace->map(
                begin, length,
                vmm::prot::read | vmm::prot::write | vmm::prot::exec,
                vmm::flag::private_ | vmm::flag::anonymous,
                obj, 0
            ))
                return reinterpret_cast<void *>(old);
        }
        else return reinterpret_cast<void *>(old); // TODO

        vmspace->brk = address;
        return addr;
    }
} // namespace syscall::memory