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

        if (const auto page = get_page_internal(idx))
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
                reinterpret_cast<void *>(lib::tohh(page) + misalign),
                buffer.subspan(progress, csize).data(),
                csize
            );
            progress += csize;
        }
        return progress;
    }

    std::uintptr_t physobject::get_page_internal(std::size_t idx)
    {
        lib::unused(idx);
        return pmm::alloc<std::uintptr_t>();
    }

    physobject::~physobject()
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

        const auto startp = lib::align_down(address, psize);
        const auto endp = lib::align_up(address + length, psize);

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

        const auto overlapping = std::ranges::to<std::vector<mapping>>(
            std::views::filter(*vmspace->tree.read_lock(), [page](const auto &entry) {
                return entry.startp <= page && page < entry.endp;
            })
        );

        if (!overlapping.empty())
        {
            lib::unused(on_write);
            // TODO
        }

        return false;
    }
} // namespace vmm