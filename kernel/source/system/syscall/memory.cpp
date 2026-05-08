// Copyright (C) 2024-2026  ilobilo

module system.syscall.memory;

import system.memory.virt;
import system.sched;
import system.vfs;
import lib;
import std;

namespace syscall::memory
{
    namespace
    {
        void *err_ptr(errnos err)
        {
            return reinterpret_cast<void *>(-static_cast<std::intptr_t>(err));
        }
    } // namespace

    void *mmap(void *addr, std::size_t length, int prot, int flags, int fd, off_t offset)
    {
        const bool priv = (flags & vmm::flag::private_);
        const bool shared = (flags & vmm::flag::shared);
        const bool anon = (flags & vmm::flag::anonymous);

        if (!(priv ^ shared) || (fd >= 0 && anon) || length == 0)
            return err_ptr(EINVAL);

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);
        if (offset < 0 || offset % npsize != 0)
            return err_ptr(EINVAL);

        length = lib::align_up(length, npsize);

        if (anon && fd != -1)
            return err_ptr(EINVAL);

        if (!anon && fd < 0)
            return err_ptr(EBADF);

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
                return err_ptr(EBADF);

            const auto &file = fdesc->file;
            const auto mflags = file->path.mnt ? file->path.mnt->flags : 0ul;

            if ((prot & vmm::prot::exec) && (mflags & vfs::ms_noexec))
                return err_ptr(EACCES);

            if (shared && (prot & vmm::prot::write) && (mflags & vfs::ms_rdonly))
                return err_ptr(EACCES);

            const auto ret = file->map();
            if (!ret.has_value())
                return err_ptr(lib::map_error(ret.error()));

            obj = *ret;
            if (!obj)
                return err_ptr(ENODEV);

            const bool is_write = vfs::is_write(file->flags);
            if (shared)
                max_prot = vmm::prot::read | (is_write ? vmm::prot::write : 0);
            else // private
                max_prot = vmm::prot::read | vmm::prot::write | vmm::prot::exec;

            if (mflags & vfs::ms_noexec)
                max_prot &= ~vmm::prot::exec;
        }
        else
        {
            if (offset != 0)
                return err_ptr(EINVAL);
            obj = new vmm::memobject { };
        }

        const auto ret = vmspace->map(
            reinterpret_cast<std::uintptr_t>(addr), length,
            prot, max_prot, flags, obj, offset
        );

        if (!ret.has_value())
            return err_ptr(lib::map_error(ret.error()));

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
        return res ? 0 : -lib::map_error(res.error());
    }

    int mprotect(void *addr, std::size_t len, int prot)
    {
        const auto proc = sched::current_process();
        const auto &vmspace = proc->vmspace;

        const auto res = vmspace->protect(
            reinterpret_cast<std::uintptr_t>(addr), len,
            static_cast<std::uint8_t>(prot)
        );

        return res ? 0 : -lib::map_error(res.error());
    }

    void *mremap(void *old_addr, std::size_t old_size, std::size_t new_size, int flags, void *new_addr)
    {
        constexpr int mremap_maymove = 1;
        constexpr int mremap_fixed = 2;
        constexpr int mremap_dontunmap = 4;

        if (flags & ~(mremap_maymove | mremap_fixed | mremap_dontunmap))
            return err_ptr(EINVAL);
        if (flags & mremap_dontunmap)
            return err_ptr(EINVAL);
        if ((flags & mremap_fixed) && !(flags & mremap_maymove))
            return err_ptr(EINVAL);
        if (old_size == 0 || new_size == 0)
            return err_ptr(EINVAL);

        const auto proc = sched::current_process();
        const auto &vmspace = proc->vmspace;

        const vmm::vmspace::remap_options opts {
            .old_addr = reinterpret_cast<std::uintptr_t>(old_addr),
            .old_len = old_size,
            .new_len = new_size,
            .may_move = (flags & mremap_maymove) != 0,
            .fixed = (flags & mremap_fixed) != 0,
            .new_addr = reinterpret_cast<std::uintptr_t>(new_addr)
        };

        const auto ret = vmspace->remap(opts);
        if (!ret.has_value())
            return err_ptr(lib::map_error(ret.error()));

        return reinterpret_cast<void *>(ret.value());
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

        if (address > old && address - vmspace->brk_start > proc->rlimits->get(sched::rlimit_data).cur)
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

    int mincore(std::size_t start, std::size_t len, unsigned char __user *vec)
    {
        lib::unused(start, len, vec);
        return -ENOSYS;
    }
} // namespace syscall::memory
