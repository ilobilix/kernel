// Copyright (C) 2024-2025  ilobilo

module system.memory.virt;

import system.memory.phys;
import system.scheduler;
import system.cpu;
import lib;
import cppstd;

import :pagemap;

namespace vmm
{
    std::size_t default_page_size()
    {
        return pagemap::from_page_size(page_size::small);
    }

    std::uintptr_t object::get_page(std::size_t idx)
    {
        auto locked = pages.lock();
        if (const auto page = locked->find(idx); page != locked->end())
            return page->second;

        if (const auto page = request_page(idx))
        {
            locked->insert({ idx, page });
            return page;
        }
        return 0;
    }

    std::size_t object::read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
    {
        const auto psize = default_page_size();
        const auto length = buffer.size_bytes();

        std::size_t progress = 0;
        while (progress < length)
        {
            const auto misalign = (progress + offset) % psize;
            const auto idx = (progress + offset) / psize;
            const auto csize = std::min(psize - misalign, length - progress);

            const auto page = get_page(idx);
            if (page == 0)
                break;

            buffer.subspan(progress, csize).copy_from(
                reinterpret_cast<std::byte *>(lib::tohh(page) + misalign)
            );
            progress += csize;
        }
        return progress;
    }

    std::size_t object::write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
    {
        const auto psize = default_page_size();
        const auto length = buffer.size_bytes();

        std::size_t progress = 0;
        while (progress < length)
        {
            const auto misalign = (progress + offset) % psize;
            const auto idx = (progress + offset) / psize;
            const auto csize = std::min(psize - misalign, length - progress);

            const auto page = get_page(idx);
            if (page == 0)
                break;

            buffer.subspan(progress, csize).copy_to(
                reinterpret_cast<std::byte *>(lib::tohh(page) + misalign)
            );
            progress += csize;
        }
        return progress;
    }

    std::size_t object::clear(std::uint64_t offset, std::uint8_t value, std::size_t length)
    {
        const auto psize = default_page_size();

        std::size_t progress = 0;
        while (progress < length)
        {
            const auto misalign = (progress + offset) % psize;
            const auto idx = (progress + offset) / psize;
            const auto csize = std::min(psize - misalign, length - progress);

            const auto page = get_page(idx);
            if (page == 0)
                break;

            std::memset(
                reinterpret_cast<void *>(lib::tohh(page) + misalign),
                value, csize
            );
            progress += csize;
        }
        return progress;
    }

    std::size_t object::copy_to(object &other, std::uint64_t offset, std::size_t length)
    {
        const auto psize = default_page_size();

        std::size_t progress = 0;
        while (progress < length)
        {
            const auto misalign = (progress + offset) % psize;
            const auto idx = (progress + offset) / psize;
            const auto csize = std::min(psize - misalign, length - progress);

            const auto our_page = get_page(idx);
            if (our_page == 0)
                break;

            const auto their_page = other.get_page(idx);
            if (their_page == 0)
                break;

            std::memcpy(
                reinterpret_cast<void *>(lib::tohh(their_page) + misalign),
                reinterpret_cast<const void *>(lib::tohh(our_page) + misalign),
                csize
            );
            progress += csize;
        }
        return progress;
    }

    std::uintptr_t memobject::request_page(std::size_t idx)
    {
        lib::unused(idx);
        return pmm::alloc<std::uintptr_t>(1, true);
    }

    void memobject::write_back() { }

    memobject::~memobject()
    {
        for (const auto &[idx, page] : *pages.lock())
            pmm::free(page);
    }

    std::expected<void, error> map_internal(
            auto &locked, auto &pmap,
            std::uintptr_t address, std::size_t length,
            std::uint8_t prot, std::uint8_t flags,
            std::shared_ptr<object> obj, off_t offset
        )
    {
        lib::bug_on(obj == nullptr);

        const auto psize = default_page_size();
        if (address % psize)
            return std::unexpected { error::addr_not_aligned };

        if (offset % psize)
            return std::unexpected { error::addr_not_aligned };

        const auto offsetp = offset / psize;

        const auto startp = address / psize;
        const auto endp = lib::div_roundup(address + length, psize);

        const auto overlapping = std::ranges::to<std::vector<mapping>>(
            std::views::filter(*locked, [startp, endp](const auto &entry) {
                return startp < entry.endp && entry.startp < endp;
            })
        );

        for (const auto &entry : overlapping)
        {
            if (entry.flags & flag::untouchable)
                return std::unexpected { error::addr_in_use };

            locked->erase(entry);

            if (startp <= entry.startp && entry.endp <= endp)
            {
                const auto addr = entry.startp * psize;
                const auto sz = entry.endp * psize - addr;
                lib::panic_if(!pmap->unmap(addr, sz, page_size::small));
            }
            else
            {
                const auto addr = std::max(startp, entry.startp) * psize;
                const auto sz = std::min(endp, entry.endp) * psize - addr;
                lib::panic_if(!pmap->unmap(addr, sz, page_size::small));

                const auto headp = startp < entry.startp ? 0 : startp - entry.startp;
                const auto tailp = endp >= entry.endp ? 0 : entry.endp - endp;

                if (headp != 0)
                {
                    locked->emplace(
                        entry.startp, entry.startp + headp,
                        entry.obj, entry.offsetp,
                        entry.prot, entry.flags
                    );
                }
                if (endp != 0)
                {
                    locked->emplace(
                        entry.endp - tailp, entry.endp,
                        entry.obj, entry.offsetp + headp + (endp - startp),
                        entry.prot, entry.flags
                    );
                }
            }
        };

        locked->emplace(
            startp, endp,
            obj, offsetp,
            prot, flags
        );

        return { };
    }

    std::expected<void, error> vmspace::map(
            std::uintptr_t address, std::size_t length,
            std::uint8_t prot, std::uint8_t flags,
            std::shared_ptr<object> obj, off_t offset
        )
    {
        auto locked = tree.write_lock();
        return map_internal(locked, pmap, address, length, prot, flags, obj, offset);
    }

    std::expected<void, error> vmspace::unmap(std::uintptr_t address, std::size_t length)
    {
        const auto psize = default_page_size();
        if (address % psize)
            return std::unexpected { error::addr_not_aligned };

        const auto startp = address / psize;
        const auto endp = lib::div_roundup(address + length, psize);

        const auto locked = tree.write_lock();

        const auto overlapping = std::ranges::to<std::vector<mapping>>(
            std::views::filter(*locked, [startp, endp](const auto &entry) {
                return startp < entry.endp && entry.startp < endp && !(entry.flags & flag::untouchable);
            })
        );

        for (const auto &entry : overlapping)
        {
            locked->erase(entry);

            if (startp <= entry.startp && entry.endp <= endp)
            {
                const auto addr = entry.startp * psize;
                const auto sz = entry.endp * psize - addr;
                lib::panic_if(!pmap->unmap(addr, sz, page_size::small));
                continue;
            }

            const auto headp = startp > entry.startp ? startp - entry.startp : 0;
            const auto tailp = endp < entry.endp ? entry.endp - endp : 0;

            if (headp != 0)
            {
                locked->emplace(
                    entry.startp, entry.startp + headp,
                    entry.obj, entry.offsetp,
                    entry.prot, entry.flags
                );
            }

            if (tailp != 0)
            {
                locked->emplace(
                    entry.endp - tailp, entry.endp,
                    entry.obj, entry.offsetp + (entry.endp - entry.startp) - tailp,
                    entry.prot, entry.flags
                );
            }

            const auto addr = std::max(startp, entry.startp) * psize;
            const auto sz = std::min(endp, entry.endp) * psize - addr;
            lib::panic_if(!pmap->unmap(addr, sz), "vmm: could not unmap region");
        };

        return { };
    }

    std::expected<void, error> vmspace::unmap(std::shared_ptr<object> obj)
    {
        lib::bug_on(obj == nullptr);

        const auto psize = default_page_size();
        const auto locked = tree.write_lock();

        const auto overlapping = std::ranges::to<std::vector<mapping>>(
            std::views::filter(*locked, [obj](const auto &entry) {
                return entry.obj == obj && !(entry.flags & flag::untouchable);
            })
        );

        for (const auto &entry : overlapping)
        {
            locked->erase(entry);

            const auto addr = entry.startp * psize;
            const auto sz = (entry.endp - entry.startp) * psize;
            lib::panic_if(!pmap->unmap(addr, sz), "vmm: could not unmap region");
        };

        return { };
    }

    std::expected<void, error> vmspace::protect(std::uintptr_t address, std::size_t length, std::uint8_t prot)
    {
        const auto psize = default_page_size();
        if (address % psize)
            return std::unexpected { error::addr_not_aligned };

        const auto pflags = [prot] {
            auto ret = pflag::user;
            if (prot & prot::read)
                ret |= pflag::read;
            if (prot & prot::write)
                ret |= pflag::write;
            if (prot & prot::exec)
                ret |= pflag::exec;
            return ret;
        } ();

        const auto pages = lib::div_roundup(length, psize);
        const auto startp = address / psize;
        const auto endp = startp + pages;

        const auto locked = tree.write_lock();

        const auto overlapping = std::ranges::to<std::vector<mapping>>(
            std::views::filter(*locked, [startp, endp](const auto &entry) {
                return startp < entry.endp && entry.startp < endp && !(entry.flags & flag::untouchable);
            })
        );

        for (const auto &entry : overlapping)
        {
            locked->erase(entry);

            if (startp <= entry.startp && entry.endp <= endp)
            {
                locked->emplace(
                    entry.startp, entry.endp,
                    entry.obj, entry.offsetp,
                    prot, entry.flags
                );

                const auto addr = entry.startp * psize;
                const auto sz = (entry.endp - entry.startp) * psize;
                lib::panic_if(!pmap->protect(addr, sz, pflags), "vmm: could not change protection flags");

                continue;
            }

            const auto headp = startp > entry.startp ? startp - entry.startp : 0;
            const auto tailp = endp < entry.endp ? entry.endp - endp : 0;

            if (headp != 0)
            {
                locked->emplace(
                    entry.startp, entry.startp + headp,
                    entry.obj, entry.offsetp,
                    entry.prot, entry.flags
                );
            }

            if (tailp != 0)
            {
                locked->emplace(
                    entry.endp - tailp, entry.endp,
                    entry.obj, entry.offsetp + (entry.endp - entry.startp) - tailp,
                    entry.prot, entry.flags
                );
            }

            locked->emplace(
                std::max(startp, entry.startp), std::min(endp, entry.endp),
                entry.obj, entry.offsetp + headp,
                prot, entry.flags
            );

            const auto addr = std::max(startp, entry.startp) * psize;
            const auto sz = std::min(endp, entry.endp) * psize - addr;
            lib::panic_if(!pmap->protect(addr, sz, pflags), "vmm: could not change protection flags");
        };

        return { };
    }

    bool vmspace::is_mapped(std::uintptr_t addr, std::size_t length)
    {
        if (length == 0)
            return false;

        const auto psize = default_page_size();

        const auto pages = lib::div_roundup(length, psize);
        const auto startp = addr / psize;
        const auto endp = startp + pages;

        const auto locked = tree.read_lock();

        auto overlapping = std::views::filter(*locked, [startp, endp](const auto &entry) {
            return startp < entry.endp && entry.startp < endp;
        });

        std::size_t covered = 0;
        for (const auto &entry : overlapping)
        {
            if (entry.endp < covered)
                continue;

            if (entry.startp > covered)
                return false;

            covered = std::max(covered, std::min(endp, entry.endp));
            if (covered >= endp)
                return true;
        }
        return covered >= endp;
    }

    std::optional<std::uintptr_t> vmspace::find_free_region(std::size_t length)
    {
        const auto psize = default_page_size();

        const auto pages = lib::div_roundup(length, psize);
        const auto locked = tree.read_lock();

        // TODO: set cap

        std::uintptr_t last_endp = 0;
        for (const auto &entry : *locked)
        {
            if (entry.startp - last_endp >= pages)
                return last_endp * psize;
            last_endp = entry.endp;
        }

        return last_endp * psize;
    }

    bool handle_pfault(std::uintptr_t addr, bool on_write)
    {
        const auto psize = default_page_size();
        const auto proc = sched::this_thread()->parent;
        const auto &vmspace = proc->vmspace;

        const auto page = addr / psize;

        std::shared_ptr<object> obj { };
        std::size_t pidx = 0;
        auto pflags = pflag::none;
        {
            const auto wlocked = vmspace->tree.write_lock();
            const auto it = std::ranges::find_if(*wlocked, [page](const auto &entry) {
                return entry.startp <= page && page < entry.endp;
            });

            if (it != wlocked->end())
            {
                auto &entry = *it;

                if (on_write && (entry.flags & flag::private_) && entry.obj.use_count() > 1)
                {
                    obj.reset(new memobject { });
                    entry.obj->copy_to(*obj,
                        (entry.startp + entry.offsetp) * psize,
                        (entry.endp - entry.startp) * psize
                    );
                    lib::panic_if(
                        !map_internal(
                            wlocked, vmspace->pmap,
                            entry.startp * psize,
                            (entry.endp - entry.startp) * psize,
                            entry.prot, entry.flags,
                            obj, entry.offsetp * psize
                        ), "vmm: could not perform copy-on-write"
                    );
                }
                else obj = entry.obj;

                pidx = (page - entry.startp) + entry.offsetp;
                pflags = [prot = entry.prot] {
                    auto ret = pflag::user;
                    if (prot & prot::read)
                        ret |= pflag::read;
                    if (prot & prot::write)
                        ret |= pflag::write;
                    if (prot & prot::exec)
                        ret |= pflag::exec;
                    return ret;
                } ();

                if (const auto pg = obj->get_page(pidx))
                {
                    if (const auto ret = vmspace->pmap->translate(page * psize, page_size::small); ret.has_value() && ret.value() == pg)
                    {
                        log::error("vmm: huh? address 0x{:X} is already mapped to 0x{:X}", page * psize, pg);
                        pflags = pflag::rwxu;
                        // return false;
                    }

                    if (vmspace->pmap->map(page * psize, pg, psize, pflags))
                        return true;
                }
            }
        }

        return false;
    }
} // namespace vmm