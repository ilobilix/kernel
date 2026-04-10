// Copyright (C) 2024-2026  ilobilo

module system.syscall.memory;

import system.memory.virt;
import system.sched;
import system.vfs;
import lib;
import std;

namespace syscall::memory
{
    void *mmap(void *addr, std::size_t length, int prot, int flags, int fd, off_t offset)
    {
        static void *invalid_addr = reinterpret_cast<void *>(-1);

        const bool priv = (flags & vmm::flag::private_);
        const bool shared = (flags & vmm::flag::shared);
        const bool anon = (flags & vmm::flag::anonymous);

        if (!(priv ^ shared) || (fd >= 0 && anon) || length == 0)
            return (errno = EINVAL, invalid_addr);

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);
        if (offset % npsize != 0)
            return (errno = EINVAL, invalid_addr);

        length = lib::align_up(length, npsize);

        if (anon && fd != -1)
            return (errno = EINVAL, invalid_addr);

        if (!anon && fd < 0)
            return (errno = EBADF, invalid_addr);

        const auto proc = sched::current_process();
        const auto &vmspace = proc->vmspace;

        vmm::prot_t max_prot = 0;
        if (anon)
            max_prot = vmm::prot::read | vmm::prot::write | vmm::prot::exec;

        vmm::object::ptr obj;
        if (!anon && fd >= 0)
        {
            auto fdesc = proc->fdt->get(static_cast<std::size_t>(fd));
            if (!fdesc)
                return (errno = EBADF, invalid_addr);

            const auto ret = fdesc->file->map();
            if (!ret.has_value())
                return (errno = lib::map_error(ret.error()), invalid_addr);

            obj = *ret;
            if (!obj)
                return (errno = ENODEV, invalid_addr);

            const bool is_write = vfs::is_write(fdesc->file->flags);
            if (shared)
                max_prot = vmm::prot::read | (is_write ? vmm::prot::write : 0);
            else // private
                max_prot = vmm::prot::read | vmm::prot::write | vmm::prot::exec;

            // TODO: mounted with noexec
        }
        else
        {
            if (offset != 0)
                return (errno = EINVAL, invalid_addr);
            obj = new vmm::memobject { };
        }

        const auto ret = vmspace->map(
            reinterpret_cast<std::uintptr_t>(addr), length,
            prot, max_prot, flags, obj, offset
        );

        if (!ret.has_value())
            return (errno = lib::map_error(ret.error()), invalid_addr);

        return reinterpret_cast<void *>(ret.value());
    }

    int munmap(void *addr, std::size_t length)
    {
        const auto proc = sched::current_process();
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->unmap(
            reinterpret_cast<std::uintptr_t>(addr),
            length
        );
        return res ? 0 : (errno = lib::map_error(res.error()), -1);
    }

    int mprotect(void *addr, std::size_t len, int prot)
    {
        const auto proc = sched::current_process();
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->protect(
            reinterpret_cast<std::uintptr_t>(addr), len,
            static_cast<std::uint8_t>(prot)
        );

        return res ? 0 : (errno = lib::map_error(res.error()), -1);
    }

    void *brk(void *addr)
    {
        const auto proc = sched::current_process();
        const auto &vmspace = proc->vmspace;

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);

        const auto address = reinterpret_cast<std::uintptr_t>(addr);
        const auto old = vmspace->current_brk;

        if (address == old || address < vmm::vmspace::mmap_min)
            return reinterpret_cast<void *>(old);

        const auto old_end = lib::align_up(old, npsize);
        const auto new_end = lib::align_up(address, npsize);

        if (address > old)
        {
            const auto begin = old_end;
            const auto length = new_end - old_end;

            if (length > 0)
            {
                const auto prot = vmm::prot::read | vmm::prot::write;
                const auto flags = vmm::flag::private_ | vmm::flag::anonymous | vmm::flag::fixed;

                const auto ret = vmspace->map(
                    begin, length,
                    prot, prot, flags,
                    nullptr, 0
                );

                if (!ret.has_value())
                    return reinterpret_cast<void *>(old);
            }
        }
        else
        {
            const auto begin = new_end;
            const auto length = old_end - new_end;

            if (length > 0)
            {
                if (!vmspace->unmap(begin, length))
                    return reinterpret_cast<void *>(old);
            }
        }

        vmspace->current_brk = address;
        return addr;
    }
} // namespace syscall::memory
