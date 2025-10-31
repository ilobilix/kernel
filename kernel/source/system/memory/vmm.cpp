// Copyright (C) 2024-2025  ilobilo

module system.memory.virt;

import system.memory.phys;
import system.scheduler;
import lib;
import cppstd;

import :pagemap;

namespace vmm
{
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

    std::size_t object::read(std::uint64_t offset, std::span<std::byte> buffer)
    {
        const auto psize = pagemap::from_page_size(page_size::small);
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

            std::memcpy(
                buffer.subspan(progress, csize).data(),
                reinterpret_cast<const void *>(lib::tohh(page) + misalign),
                csize
            );
            progress += csize;
        }
        return progress;
    }

    std::size_t object::write(std::uint64_t offset, std::span<std::byte> buffer)
    {
        const auto psize = pagemap::from_page_size(page_size::small);
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

            std::memcpy(
                reinterpret_cast<void *>(page + misalign),
                buffer.subspan(progress, csize).data(),
                csize
            );
            progress += csize;
        }
        return progress;
    }

    std::uintptr_t memobject::request_page(std::size_t idx)
    {
        lib::unused(idx);
        return pmm::alloc<std::uintptr_t>();
    }

    void memobject::write_back() { }

    memobject::~memobject()
    {
        for (const auto &[idx, page] : *pages.lock())
            pmm::free(page);
    }

    std::expected<void, error> vmspace::map(
            std::uintptr_t address, std::size_t length,
            std::uint8_t prot, std::uint8_t flags,
            std::shared_ptr<object> obj, off_t offset
        )
    {
        lib::bug_on(obj == nullptr);

        const auto psize = pagemap::from_page_size(page_size::small);
        if (address % psize)
            return std::unexpected { error::addr_not_aligned };

        const auto startp = lib::div_rounddown(address, psize);
        const auto endp = lib::div_roundup(address + length, psize);

        const auto locked = tree.write_lock();

        auto overlapping = std::views::filter(*locked, [startp, endp](const auto &entry) {
            return startp < entry.endp && entry.startp < endp;
        });

        for (const auto &entry : overlapping)
        {
            locked->erase(entry);

            if (startp <= entry.startp && entry.endp <= endp)
            {
                const auto addr = entry.startp * psize;
                const auto sz = entry.endp * psize - addr;
                lib::bug_on(!pmap->unmap(addr, sz, page_size::small));
            }
            else
            {
                const auto addr = std::max(startp, entry.startp) * psize;
                const auto sz = std::min(endp, entry.endp) * psize - addr;
                lib::bug_on(!pmap->unmap(addr, sz, page_size::small));

                const auto headp = startp < entry.startp ? 0 : startp - entry.startp;
                const auto tailp = endp >= entry.endp ? 0 : entry.endp - endp;

                if (headp != 0)
                {
                    locked->emplace(
                        entry.startp, entry.startp + headp,
                        entry.obj, entry.offset,
                        entry.prot, entry.flags
                    );
                }
                if (endp != 0)
                {
                    locked->emplace(
                        entry.endp - tailp, entry.endp,
                        entry.obj, entry.offset + headp + endp - startp,
                        entry.prot, entry.flags
                    );
                }
            }
        };

        locked->emplace(
            startp, endp,
            obj, offset,
            prot, flags
        );

        return { };
    }

    bool vmspace::is_mapped(std::uintptr_t addr, std::size_t length)
    {
        const auto psize = pagemap::from_page_size(page_size::small);

        const auto pages = lib::div_roundup(length, psize);
        const auto startp = addr / psize;
        const auto endp = startp + pages;

        const auto locked = tree.read_lock();

        auto overlapping = std::views::filter(*locked, [startp, endp](const auto &entry) {
            return startp < entry.endp && entry.startp < endp;
        });

        std::optional<std::uintptr_t> prev { std::nullopt };
        for (const auto &entry : overlapping)
        {
            if (prev.has_value() && prev.value() + 1 != entry.startp)
                return false;
            prev = entry.endp;
        }
        return prev.has_value();
    }

    bool handle_pfault(std::uintptr_t addr, bool on_write)
    {
        const auto psize = pagemap::from_page_size(page_size::small);
        const auto thread = sched::this_thread();
        const auto proc = sched::proc_for(thread->pid);
        const auto vmspace = proc->vmspace;

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

                if (on_write && (entry.flags & flag::cow))
                {
                    if (entry.obj.use_count() > 1)
                    {
                        obj.reset(new memobject { });
                        for (std::size_t i = 0; i < entry.endp - entry.startp; i++)
                        {
                            lib::membuffer buf { psize };
                            const std::size_t offset = i * psize + entry.offset;

                            entry.obj->read(offset, buf.span());
                            obj->write(offset, buf.span());
                        }
                        lib::bug_on(!vmspace->map(
                            entry.startp * psize,
                            (entry.endp - entry.startp) * psize,
                            entry.prot, entry.flags & ~flag::cow,
                            obj, entry.offset
                        ));
                        goto exit;
                    }
                    entry.flags &= ~flag::cow;
                }
                obj = entry.obj;

                exit:
                pidx = (page - entry.startp) + entry.offset * psize;
                pflags = [prot = entry.prot] {
                    auto ret = pflag::none;
                    if (prot & prot::read)
                        ret |= pflag::read;
                    if (prot & prot::write)
                        ret |= pflag::write;
                    if (prot & prot::exec)
                        ret |= pflag::exec;
                    return ret;
                } ();
            }

            if (obj != nullptr)
            {
                if (const auto pg = obj->get_page(pidx))
                {
                    if (vmspace->pmap->map(page * psize, pg, psize, pflags))
                        return true;
                }
            }
        }

        return false;
    }
} // namespace vmm