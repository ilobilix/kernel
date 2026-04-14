// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>

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

        if ((priv && shared) || (!priv && !shared))
        {
            errno = EINVAL;
            return invalid_addr;
        }

        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);
        const auto &vmspace = proc->vmspace;

        std::uintptr_t address = reinterpret_cast<std::uintptr_t>(addr);
        if (fixed == false)
        {
            address = vmspace->find_free_region(length);
            if (address == 0)
            {
                errno = ENOMEM;
                return invalid_addr;
            }
        }
        else if (address == 0)
        {
            errno = EINVAL;
            return invalid_addr;
        }

        std::shared_ptr<vmm::object> obj;
        if (!anon)
        {
            lib::unused(fd);
            lib::panic("todo: mmap files");
        }
        else obj = std::make_shared<vmm::memobject>();

        lib::bug_on(!vmspace->map(
            address, length,
            static_cast<std::uint8_t>(prot),
            static_cast<std::uint8_t>(flags),
            obj, static_cast<off_t>(offset)
        ));

        return reinterpret_cast<void *>(address);
    }

    int munmap(void *addr, std::size_t length)
    {
        lib::unused(addr, length);
        errno = ENOSYS;
        return -1;
    }
} // namespace syscall::memory