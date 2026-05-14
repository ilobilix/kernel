// Copyright (C) 2024-2026  ilobilo

module system.memory.virt;

import system.memory.va;
import system.sched;
import system.cpu;
import magic_enum;

import :pagemap;

namespace vmm
{
    using namespace magic_enum::bitwise_operators;

    namespace
    {
        std::array<std::atomic<std::size_t>, 2> stats { };
        std::atomic<std::size_t> &stats_for(object_type type)
        {
            return stats[static_cast<std::size_t>(type)];
        }

        constexpr std::size_t num_waitqueues = 256;
        sched::wait_queue_t waitqueues[num_waitqueues];

        std::size_t hash_page(page *pg)
        {
            const auto addr = reinterpret_cast<std::uintptr_t>(pg);
            return (addr >> std::countr_zero(sizeof(page))) % num_waitqueues;
        }

        void wait_on_busy_page(page *pg)
        {
            auto &wq = waitqueues[hash_page(pg)];
            while (true)
            {
                const auto gen = wq.snapshot_gen();
                if (!(pg->flags.load(std::memory_order_acquire) & page::flag::busy))
                    break;
                wq.wait_prepared(gen);
            }
        }

        std::uintptr_t pfndb_base()
        {
            static const auto cached = [] {
                return pmm::info().pfndb_base;
            } ();
            return cached;
        }

        pflag prot_to_pflags(prot_t prot)
        {
            auto ret = pflag::user;
            if (prot & prot::read)
                ret |= pflag::read;
            if (prot & prot::write)
                ret |= pflag::write;
            if (prot & prot::exec)
                ret |= pflag::exec;
            return ret;
        }

        bool valid_user_range(std::uintptr_t address, std::size_t length)
        {
            return length <= vmspace::vspace_top && address >= vmspace::mmap_min &&
                address <= vmspace::vspace_top - length;
        }
    } // namespace

    std::size_t cached_pages(object_type type)
    {
        return stats_for(type).load(std::memory_order_relaxed);
    }

    page *page_for(std::uintptr_t addr)
    {
        const auto idx = lib::fromhh(addr) / pmm::page_size;
        const auto pg = reinterpret_cast<page *>(pfndb_base() + idx * sizeof(page));
        return pg;
    }

    std::uintptr_t paddr_from(page *pg)
    {
        const auto idx = (reinterpret_cast<std::uintptr_t>(pg) - pfndb_base()) / sizeof(page);
        return idx * pmm::page_size;
    }

    lib::expect<void> object::read_pages(std::uint64_t offp, std::span<page *> pages, std::size_t idx)
    {
        lib::bug_on(pages.size() > max_readahead);

        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        const auto num_alloc_pages = npsize / pmm::page_size;

        const auto start_idx = offp;

        // max_readahead is aligned to 8
        std::uint8_t data[max_readahead / 8] { 0 };
        lib::bitmap needs_fetch { data, max_readahead };

        {
            auto locked = cache.lock();
            for (std::size_t i = 0; i < pages.size(); i++)
            {
                if (auto it = locked->find(start_idx + i); it != locked->end())
                {
                    auto pg = it->second;
                    pg->ref();

                    if (pg->flags.load(std::memory_order_acquire) & page::flag::busy)
                    {
                        locked.unlock();
                        wait_on_busy_page(pg);
                        locked.lock();

                        if (pg->unref())
                            pmm::free(paddr_from(pg), num_alloc_pages);

                        i--;
                        continue;
                    }
                    pages[i] = pg;
                    continue;
                }

                const auto paddr = pmm::alloc(num_alloc_pages, true); // zeroed out
                if (paddr == 0)
                {
                    if (i > idx)
                    {
                        for (std::size_t j = i; j < pages.size(); j++)
                            pages[j] = nullptr;
                        break;
                    }

                    for (std::size_t j = 0; j < i; j++)
                    {
                        auto pg = pages[j];
                        if (!needs_fetch[j])
                        {
                            if (pg && pg->unref())
                                pmm::free(paddr_from(pg), num_alloc_pages);
                            continue;
                        }

                        auto it = locked->find(start_idx + j);
                        const bool in_cache = (it != locked->end() && it->second == pg);

                        if (in_cache)
                        {
                            locked->erase(it);
                            stats_for(type).fetch_sub(1, std::memory_order_relaxed);
                            pg->obj_ptr = nullptr;
                        }

                        pg->flags.fetch_and(~page::flag::busy, std::memory_order_release);

                        auto &wq = waitqueues[hash_page(pg)];
                        wq.wake_all();

                        if (pg->unref(in_cache ? 2 : 1))
                            pmm::free(paddr_from(pg), num_alloc_pages);

                        pages[j] = nullptr;
                    }
                    return std::unexpected { lib::err::out_of_memory };
                }

                auto pg = page_for(paddr);
                pg->refcount.store(2, std::memory_order_relaxed);
                pg->obj_ptr = this;
                pg->offp = start_idx + i;
                pg->flags.store(page::flag::file | page::flag::busy, std::memory_order_relaxed);

                locked->insert({ start_idx + i, pg });
                stats_for(type).fetch_add(1, std::memory_order_relaxed);
                pages[i] = pg;
                needs_fetch[i] = true;
            }
        }

        for (std::size_t i = 0; i < pages.size(); )
        {
            if (!needs_fetch[i])
            {
                i++;
                continue;
            }

            std::size_t num_pages = 1;
            while (i + num_pages < pages.size() && needs_fetch[i + num_pages])
                num_pages++;

            const auto chunk = pages.subspan(i, num_pages);
            const auto chunk_off = offp + i;

            auto res = fetch_pages(chunk_off, chunk);
            if (!res.has_value())
            {
                auto locked = cache.lock();
                for (std::size_t j = i; j < pages.size(); j++)
                {
                    auto pg = pages[j];
                    if (!needs_fetch[j])
                    {
                        if (pg->unref())
                            pmm::free(paddr_from(pg), num_alloc_pages);
                        continue;
                    }

                    auto it = locked->find(start_idx + j);
                    const bool in_cache = (it != locked->end() && it->second == pg);

                    if (in_cache)
                    {
                        locked->erase(it);
                        stats_for(type).fetch_sub(1, std::memory_order_relaxed);
                        pg->obj_ptr = nullptr;
                    }

                    pg->flags.fetch_and(~page::flag::busy, std::memory_order_release);

                    auto &wq = waitqueues[hash_page(pg)];
                    wq.wake_all();

                    if (pg->unref(in_cache ? 2 : 1))
                        pmm::free(paddr_from(pg), num_alloc_pages);

                    pages[j] = nullptr;
                }

                if (idx >= i)
                    return res;
                return { };
            }

            for (const auto pg : chunk)
            {
                pg->flags.fetch_and(~page::flag::busy, std::memory_order_release);
                auto &wq = waitqueues[hash_page(pg)];
                wq.wake_all();
            }

            i += num_pages;
        }

        return { };
    }

    lib::expect<void> object::write_back(std::uint64_t offp, std::size_t num_pages)
    {
        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        const auto num_alloc_pages = npsize / pmm::page_size;

        page *chunk[max_readahead];

        const auto end_idx = offp + num_pages;
        for (std::size_t i = offp; i < end_idx; )
        {
            std::size_t chunk_size = 0;
            {
                auto locked = cache.lock();

                while (i + chunk_size < end_idx && chunk_size < max_readahead)
                {
                    auto it = locked->find(i + chunk_size);
                    if (it == locked->end())
                    {
                        if (chunk_size > 0)
                            break;

                        i++;
                        continue;
                    }

                    auto pg = it->second;
                    auto flags = pg->flags.load(std::memory_order_acquire);

                    if (flags & page::flag::busy)
                    {
                        if (chunk_size > 0)
                            break;

                        locked.unlock();
                        wait_on_busy_page(pg);
                        locked.lock();
                        continue;
                    }

                    if (!(flags & page::flag::dirty))
                    {
                        if (chunk_size > 0)
                            break;
                        i++;
                        continue;
                    }

                    auto old_flags = flags;
                    bool changed = false;
                    while (!pg->flags.compare_exchange_weak(
                        old_flags, (old_flags | page::flag::busy) & ~page::flag::dirty,
                        std::memory_order_relaxed
                    )) {
                        if (!(old_flags & page::flag::dirty) || (old_flags & page::flag::busy))
                        {
                            changed = true;
                            break;
                        }
                    }

                    if (changed)
                    {
                        if (chunk_size > 0)
                            break;
                        continue;
                    }

                    pg->ref();
                    chunk[chunk_size++] = pg;
                }
            }

            if (chunk_size == 0)
                continue;

            std::span<page *> pages { chunk, chunk_size };
            const auto chunk_off = i;

            auto res = write_pages(chunk_off, pages);
            if (!res.has_value())
            {
                for (const auto pg : pages)
                {
                    pg->flags.fetch_or(page::flag::dirty, std::memory_order_relaxed);
                    pg->flags.fetch_and(~page::flag::busy, std::memory_order_release);

                    auto &wq = waitqueues[hash_page(pg)];
                    wq.wake_all();

                    if (pg->unref())
                        pmm::free(paddr_from(pg), num_alloc_pages);
                }
                return res;
            }

            for (const auto pg : pages)
            {
                pg->flags.fetch_and(~page::flag::busy, std::memory_order_release);

                auto &wq = waitqueues[hash_page(pg)];
                wq.wake_all();

                if (pg->unref())
                    pmm::free(paddr_from(pg), num_alloc_pages);
            }

            i += chunk_size;
        }

        return { };
    }

    std::size_t object::apply_func(std::uint64_t offset, std::size_t size, auto func)
    {
        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        const auto num_alloc_pages = npsize / pmm::page_size;

        std::size_t progress = 0;
        std::size_t remaining = size;

        std::size_t curr_idx = offset / npsize;
        std::size_t poffset = offset % npsize;

        page *chunk[max_readahead];

        while (remaining > 0)
        {
            const auto num_pages = std::min(
                (poffset + remaining + npsize - 1) / npsize,
                max_readahead
            );
            std::span<page *> pages { chunk, num_pages };

            auto res = read_pages(curr_idx, pages, 0);
            if (!res.has_value())
                break;

            const auto cleanup = [&](std::size_t i) {
                for (std::size_t j = i + 1; j < num_pages; j++)
                {
                    if (chunk[j] && chunk[j]->unref())
                        pmm::free(paddr_from(chunk[j]), num_alloc_pages);
                }
            };

            for (std::size_t i = 0; i < num_pages && remaining > 0; i++)
            {
                auto pg = chunk[i];
                if (!pg)
                {
                    cleanup(i);
                    return progress;
                }

                const auto vaddr = lib::tohh(paddr_from(pg));
                const auto copy_len = std::min(npsize - poffset, remaining);

                bool success = func(pg, progress, vaddr + poffset, copy_len);

                if (success)
                {
                    progress += copy_len;
                    remaining -= copy_len;
                }

                if (pg->unref())
                    pmm::free(paddr_from(pg), num_alloc_pages);

                if (!success)
                {
                    cleanup(i);
                    return progress;
                }

                poffset = 0;
            }

            curr_idx += num_pages;
        }

        return progress;
    }

    std::size_t object::read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
    {
        if (buffer.empty())
            return 0;

        return apply_func(offset, buffer.size(),
            [&buffer](page *pg, std::size_t progress, std::uintptr_t addr, std::size_t len)
            {
                lib::unused(pg);

                const std::span<const std::byte> src {
                    reinterpret_cast<const std::byte *>(addr), len
                };
                return buffer.subspan(progress, len).copy_from(src);
            }
        );
    }

    std::size_t object::write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
    {
        if (buffer.empty())
            return 0;

        return apply_func(offset, buffer.size(),
            [&buffer](page *pg, std::size_t progress, std::uintptr_t addr, std::size_t len)
            {
                const std::span<std::byte> dest {
                    reinterpret_cast<std::byte *>(addr), len
                };
                bool ret = buffer.subspan(progress, len).copy_to(dest);
                if (ret)
                    pg->flags.fetch_or(page::flag::dirty, std::memory_order_relaxed);
                return ret;
            }
        );
    }

    std::size_t object::clear(std::uint64_t offset, std::uint8_t value, std::size_t length)
    {
        if (length == 0)
            return 0;

        return apply_func(offset, length,
            [value](page *pg, std::size_t progress, std::uintptr_t addr, std::size_t len)
            {
                lib::unused(progress);

                auto dest = reinterpret_cast<std::byte *>(addr);
                std::memset(dest, value, len);

                pg->flags.fetch_or(page::flag::dirty, std::memory_order_relaxed);
                return true;
            }
        );
    }

    object::~object()
    {
        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        const auto num_alloc_pages = npsize / pmm::page_size;

        auto locked = cache.lock();
        if (!locked->empty())
            stats_for(type).fetch_sub(locked->size(), std::memory_order_relaxed);

        for (auto &[_, page] : *locked)
        {
            if (page && page->unref())
                pmm::free(paddr_from(page), num_alloc_pages);
        }
    }

    lib::expect<std::uintptr_t> vmspace::map(
        std::uintptr_t hint, std::size_t length,
        prot_t prot, prot_t max_prot, flag_t flags,
        object::ptr obj, std::uint64_t offset
    )
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        if (((flags & flag::shared) != 0) + ((flags & flag::private_) != 0) != 1)
            return std::unexpected { lib::err::invalid_flags };

        if ((max_prot & prot) != prot)
            return std::unexpected { lib::err::permission_denied };

        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        if (obj && offset % npsize)
            return std::unexpected { lib::err::addr_not_aligned };

        const auto offp = offset / npsize;

        const auto orig_length = length;
        length = lib::align_up(length, npsize);
        if (length < orig_length)
            return std::unexpected { lib::err::invalid_length };

        object::ptr target_obj;
        anon_map::ptr target_amap;

        const bool is_anon = flags & flag::anonymous;
        const bool is_file = !is_anon;

        if (is_file)
            target_obj = std::move(obj);

        if (flags & flag::private_)
        {
            target_amap = new anon_map { };
            target_amap->nslots = length / npsize;
            target_amap->slots = std::make_unique<anon::ptr []>(target_amap->nslots);
        }
        else if ((flags & flag::shared) && (flags & flag::anonymous))
            target_obj = new memobject { object_type::shmem };

        std::uintptr_t startp = 0;
        std::uintptr_t endp = 0;

        auto locked = tree.lock();

        if ((flags & flag::fixed) || (flags & flag::fixed_noreplace))
        {
            if (hint % npsize)
                return std::unexpected { lib::err::addr_not_aligned };

            if (!valid_user_range(hint, length))
                return std::unexpected { lib::err::addr_out_of_bounds };

            startp = hint / npsize;
            endp = (hint + length) / npsize;

            {
                const auto overlapping = locked->overlapping(startp, endp);
                if (!overlapping.empty() && (flags & flag::fixed_noreplace))
                    return std::unexpected { lib::err::already_exists };

                for (auto &ent : overlapping)
                {
                    if (ent.flags & flag::untouchable)
                        return std::unexpected { lib::err::not_permitted };
                }
            }

            while (true)
            {
                const auto overlapping = locked->overlapping(startp, endp);

                auto it = overlapping.begin();
                if (it == overlapping.end())
                    break;

                auto ent = it.value();

                const auto overlap_start = std::max(startp, ent->startp);
                const auto overlap_end = std::min(endp, ent->endp);

                const auto unmap_vaddr = overlap_start * npsize;
                const auto unmap_length = (overlap_end - overlap_start) * npsize;

                if (const auto ret = pmap->unmap(unmap_vaddr, unmap_length, psize); !ret)
                    return std::unexpected { ret.error() };

                locked->remove(ent);

                if (ent->endp > endp)
                {
                    const auto pages = endp - ent->startp;

                    const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                    const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;

                    locked->insert(new entry {
                        .startp = endp,
                        .endp = ent->endp,
                        .obj = ent->obj,
                        .offp = obj_offp,
                        .amap = ent->amap,
                        .anon_idx = anon_idx,
                        .prot = ent->prot,
                        .max_prot = ent->max_prot,
                        .flags = ent->flags,
                        .hook = { },
                        .interval = { }
                    });
                }

                if (ent->startp < startp)
                {
                    ent->endp = startp;
                    locked->insert(ent);
                }
                else delete ent;
            }
        }
        else
        {
            const auto ret = find_free_region_internal(locked, length);
            if (!ret)
                return std::unexpected { lib::err::out_of_memory };

            startp = *ret / npsize;
            endp = (*ret + length) / npsize;
        }

        locked->insert(new entry {
            .startp = startp,
            .endp = endp,
            .obj = std::move(target_obj),
            .offp = is_file ? offp : 0,
            .amap = std::move(target_amap),
            .anon_idx = 0,
            .prot = prot,
            .max_prot = max_prot,
            .flags = flags,
            .hook = { },
            .interval = { }
        });

        return startp * npsize;
    }

    lib::expect<void> vmspace::unmap(std::uintptr_t address, std::size_t length)
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        if (address % npsize)
            return std::unexpected { lib::err::addr_not_aligned };

        const auto orig_length = length;
        length = lib::align_up(length, npsize);
        if (length < orig_length)
            return std::unexpected { lib::err::invalid_length };

        if (!valid_user_range(address, length))
            return std::unexpected { lib::err::addr_out_of_bounds };

        const auto startp = address / npsize;
        const auto endp = (address + length) / npsize;

        auto locked = tree.lock();
        {
            const auto overlapping = locked->overlapping(startp, endp);

            for (const auto &ent : overlapping)
            {
                if (ent.flags & flag::untouchable)
                    return std::unexpected { lib::err::not_permitted };
            }
        }

        while (true)
        {
            const auto overlapping = locked->overlapping(startp, endp);

            auto it = overlapping.begin();
            if (it == overlapping.end())
                break;

            auto ent = it.value();

            const auto overlap_start = std::max(startp, ent->startp);
            const auto overlap_end = std::min(endp, ent->endp);

            const auto unmap_vaddr = overlap_start * npsize;
            const auto unmap_length = (overlap_end - overlap_start) * npsize;

            if (const auto ret = pmap->unmap(unmap_vaddr, unmap_length, psize); !ret)
                return std::unexpected { ret.error() };

            locked->remove(ent);

            if (ent->endp > overlap_end)
            {
                const auto pages = overlap_end - ent->startp;

                const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;

                locked->insert(new entry {
                    .startp = overlap_end,
                    .endp = ent->endp,
                    .obj = ent->obj,
                    .offp = obj_offp,
                    .amap = ent->amap,
                    .anon_idx = anon_idx,
                    .prot = ent->prot,
                    .max_prot = ent->max_prot,
                    .flags = ent->flags,
                    .hook = { },
                    .interval = { }
                });
            }

            if (ent->startp < overlap_start)
            {
                locked->insert(new entry {
                    .startp = ent->startp,
                    .endp = overlap_start,
                    .obj = ent->obj,
                    .offp = ent->offp,
                    .amap = ent->amap,
                    .anon_idx = ent->anon_idx,
                    .prot = ent->prot,
                    .max_prot = ent->max_prot,
                    .flags = ent->flags,
                    .hook = { },
                    .interval = { }
                });
            }

            delete ent;
        }

        return { };
    }

    lib::expect<void> vmspace::protect(std::uintptr_t address, std::size_t length, prot_t prot)
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        if (address % npsize)
            return std::unexpected { lib::err::addr_not_aligned };

        const auto orig_length = length;
        length = lib::align_up(length, npsize);
        if (length < orig_length)
            return std::unexpected { lib::err::invalid_length };

        if (!valid_user_range(address, length))
            return std::unexpected { lib::err::addr_out_of_bounds };

        auto startp = address / npsize;
        const auto endp = (address + length) / npsize;

        auto locked = tree.lock();
        {
            const auto overlapping = locked->overlapping(startp, endp);

            const auto total_pages = std::accumulate(
                std::ranges::begin(overlapping), std::ranges::end(overlapping), 0uz,
                [startp, endp](std::size_t acc, const entry &ent) {
                    const auto overlap_start = std::max(startp, ent.startp);
                    const auto overlap_end = std::min(endp, ent.endp);
                    return acc + (overlap_end - overlap_start);
                }
            );

            if (total_pages < (endp - startp))
                return std::unexpected { lib::err::out_of_memory };

            for (const auto &ent : overlapping)
            {
                if (ent.flags & flag::untouchable)
                    return std::unexpected { lib::err::not_permitted };

                if ((ent.max_prot & prot) != prot)
                    return std::unexpected { lib::err::permission_denied };
            }
        }

        while (startp < endp)
        {
            const auto overlapping = locked->overlapping(startp, endp);

            auto it = overlapping.begin();
            if (it == overlapping.end())
                break;

            auto ent = it.value();

            const auto overlap_start = std::max(startp, ent->startp);
            const auto overlap_end = std::min(endp, ent->endp);

            locked->remove(ent);

            if (ent->endp > overlap_end)
            {
                const auto pages = overlap_end - ent->startp;

                const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;

                locked->insert(new entry {
                    .startp = overlap_end,
                    .endp = ent->endp,
                    .obj = ent->obj,
                    .offp = obj_offp,
                    .amap = ent->amap,
                    .anon_idx = anon_idx,
                    .prot = ent->prot,
                    .max_prot = ent->max_prot,
                    .flags = ent->flags,
                    .hook = { },
                    .interval = { }
                });
            }

            if (ent->startp < overlap_start)
            {
                locked->insert(new entry {
                    .startp = ent->startp,
                    .endp = overlap_start,
                    .obj = ent->obj,
                    .offp = ent->offp,
                    .amap = ent->amap,
                    .anon_idx = ent->anon_idx,
                    .prot = ent->prot,
                    .max_prot = ent->max_prot,
                    .flags = ent->flags,
                    .hook = { },
                    .interval = { }
                });
            }

            const auto pages = overlap_start - ent->startp;

            ent->startp = overlap_start;
            ent->endp = overlap_end;
            if (ent->obj)
                ent->offp += pages;
            if (ent->amap)
                ent->anon_idx += pages;
            ent->prot = prot;

            locked->insert(ent);

            auto tgt_prot = prot;
            if ((ent->flags & flag::private_) && (tgt_prot & prot::write))
                tgt_prot &= ~prot::write;

            const auto vaddr = overlap_start * npsize;
            const auto len = (overlap_end - overlap_start) * npsize;

            if (const auto ret = pmap->protect(vaddr, len, prot_to_pflags(tgt_prot), psize); !ret)
            {
                if (ret.error() != lib::err::not_mapped)
                    return std::unexpected { ret.error() };
            }

            startp = overlap_end;
        }

        return { };
    }

    lib::expect<std::uintptr_t> vmspace::remap(const remap_options &opts)
    {
        if (opts.old_len == 0 || opts.new_len == 0)
            return std::unexpected { lib::err::invalid_length };

        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);

        if (opts.old_addr % npsize)
            return std::unexpected { lib::err::addr_not_aligned };
        if (opts.fixed && opts.new_addr % npsize)
            return std::unexpected { lib::err::addr_not_aligned };

        const auto old_len = lib::align_up(opts.old_len, npsize);
        const auto new_len = lib::align_up(opts.new_len, npsize);
        if (old_len < opts.old_len || new_len < opts.new_len)
            return std::unexpected { lib::err::invalid_length };

        if (!valid_user_range(opts.old_addr, old_len))
            return std::unexpected { lib::err::addr_out_of_bounds };
        if (opts.fixed && !valid_user_range(opts.new_addr, new_len))
            return std::unexpected { lib::err::addr_out_of_bounds };

        if (opts.fixed && !opts.may_move)
            return std::unexpected { lib::err::invalid_flags };

        const auto old_startp = opts.old_addr / npsize;
        const auto old_endp = (opts.old_addr + old_len) / npsize;

        auto locked = tree.lock();
        entry *src = nullptr;
        {
            auto overlapping = locked->overlapping(old_startp, old_endp);
            auto it = overlapping.begin();
            if (it == overlapping.end())
                return std::unexpected { lib::err::invalid_address };

            src = it.value();
            it++;
            if (it != overlapping.end())
                return std::unexpected { lib::err::invalid_address };
            if (src->startp != old_startp || src->endp != old_endp)
                return std::unexpected { lib::err::invalid_address };
            if (src->flags & flag::untouchable)
                return std::unexpected { lib::err::not_permitted };
        }

        if (new_len == old_len && !opts.fixed)
            return opts.old_addr;

        if (new_len < old_len)
        {
            const auto drop_start = old_startp + (new_len / npsize);
            const auto drop_pages = old_endp - drop_start;
            const auto vaddr = drop_start * npsize;
            const auto length = drop_pages * npsize;

            if (const auto ret = pmap->unmap(vaddr, length, psize); !ret)
                return std::unexpected { ret.error() };

            locked->remove(src);
            src->endp = drop_start;
            locked->insert(src);

            return opts.old_addr;
        }

        std::uintptr_t dst_startp = 0;
        if (opts.fixed)
        {
            const auto target_startp = opts.new_addr / npsize;
            const auto target_endp = (opts.new_addr + new_len) / npsize;

            if (target_startp < old_endp && target_endp > old_startp)
                return std::unexpected { lib::err::invalid_argument };

            {
                const auto overlap = locked->overlapping(target_startp, target_endp);
                for (const auto &ent : overlap)
                {
                    if (ent.flags & flag::untouchable)
                        return std::unexpected { lib::err::not_permitted };
                }
            }

            while (true)
            {
                const auto overlap = locked->overlapping(target_startp, target_endp);
                auto it = overlap.begin();
                if (it == overlap.end())
                    break;
                auto ent = it.value();

                const auto ovs = std::max(target_startp, ent->startp);
                const auto ove = std::min(target_endp, ent->endp);

                const auto unmap_vaddr = ovs * npsize;
                const auto unmap_length = (ove - ovs) * npsize;

                if (const auto ret = pmap->unmap(unmap_vaddr, unmap_length, psize); !ret)
                    return std::unexpected { ret.error() };

                locked->remove(ent);

                if (ent->endp > target_endp)
                {
                    const auto pages = target_endp - ent->startp;
                    const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                    const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;
                    locked->insert(new entry {
                        .startp = target_endp,
                        .endp = ent->endp,
                        .obj = ent->obj,
                        .offp = obj_offp,
                        .amap = ent->amap,
                        .anon_idx = anon_idx,
                        .prot = ent->prot,
                        .max_prot = ent->max_prot,
                        .flags = ent->flags,
                        .hook = { },
                        .interval = { }
                    });
                }
                if (ent->startp < target_startp)
                {
                    ent->endp = target_startp;
                    locked->insert(ent);
                }
                else delete ent;
            }
            dst_startp = target_startp;
        }
        else if (!opts.may_move)
        {
            const auto grow_addr = opts.old_addr + old_len;
            const auto grow_bytes = new_len - old_len;
            if (!valid_user_range(grow_addr, grow_bytes))
                return std::unexpected { lib::err::addr_out_of_bounds };

            const auto grow_pages = grow_bytes / npsize;
            const auto grow_start = old_endp;
            const auto grow_end = old_endp + grow_pages;

            const auto overlap = locked->overlapping(grow_start, grow_end);
            if (!overlap.empty())
                return std::unexpected { lib::err::no_space_left };

            if (src->obj && !src->amap)
            {
                locked->remove(src);
                src->endp = grow_end;
                locked->insert(src);
            }
            else
            {
                anon_map::ptr tail_amap { new anon_map { } };
                tail_amap->nslots = grow_pages;
                tail_amap->slots = std::make_unique<anon::ptr []>(grow_pages);

                const auto tail_offp = (src->obj && src->amap)
                    ? src->offp + (old_len / npsize) : 0;

                locked->insert(new entry {
                    .startp = grow_start,
                    .endp = grow_end,
                    .obj = (src->obj && src->amap) ? src->obj : object::ptr { },
                    .offp = tail_offp,
                    .amap = std::move(tail_amap),
                    .anon_idx = 0,
                    .prot = src->prot,
                    .max_prot = src->max_prot,
                    .flags = src->flags,
                    .hook = { },
                    .interval = { }
                });
            }
            return opts.old_addr;
        }
        else
        {
            const auto found = find_free_region_internal(locked, new_len);
            if (!found)
                return std::unexpected { lib::err::out_of_memory };
            dst_startp = *found / npsize;
        }

        const auto src_pages = old_len / npsize;
        const auto dst_endp = dst_startp + new_len / npsize;
        const auto dst_old_endp = dst_startp + src_pages;

        if (const auto ret = pmap->unmap(opts.old_addr, old_len, psize); !ret)
            return std::unexpected { ret.error() };

        locked->remove(src);
        src->startp = dst_startp;
        src->endp = dst_old_endp;
        locked->insert(src);

        if (new_len > old_len)
        {
            const auto tail_pages = (new_len - old_len) / npsize;

            if (src->obj && !src->amap)
            {
                locked->remove(src);
                src->endp = dst_endp;
                locked->insert(src);
            }
            else
            {
                anon_map::ptr tail_amap { new anon_map { } };
                tail_amap->nslots = tail_pages;
                tail_amap->slots = std::make_unique<anon::ptr []>(tail_pages);

                locked->insert(new entry {
                    .startp = dst_old_endp,
                    .endp = dst_endp,
                    .obj = (src->obj && src->amap) ? src->obj : object::ptr { },
                    .offp = (src->obj && src->amap) ? src->offp + src_pages : 0,
                    .amap = std::move(tail_amap),
                    .anon_idx = 0,
                    .prot = src->prot,
                    .max_prot = src->max_prot,
                    .flags = src->flags,
                    .hook = { },
                    .interval = { }
                });
            }
        }

        return dst_startp * npsize;
    }

    lib::expect<std::uintptr_t> vmspace::find_free_region_internal(auto &locked, std::size_t length)
    {
        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);

        const auto pages = lib::div_roundup(length, npsize);
        const auto pmin = mmap_min / npsize;
        const auto pmax = mmap_max / npsize;

        const auto page = va::find_free_region(*locked, pmin, pmax, pages);
        if (!page)
            return std::unexpected { lib::err::out_of_memory };

        return *page * npsize;
    }

    lib::expect<std::uintptr_t> vmspace::find_free_region(std::size_t length)
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        const auto locked = tree.lock();
        return find_free_region_internal(locked, length);
    }

    std::shared_ptr<vmspace> vmspace::fork(std::shared_ptr<vmm::pagemap> cpmap)
    {
        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);

        auto ret = std::make_shared<vmspace>();
        ret->pmap = cpmap;
        ret->brk_start = brk_start;
        ret->current_brk = current_brk;

        auto locked = tree.lock();
        auto clocked = ret->tree.lock();

        for (const auto &ent : *locked)
        {
            if (ent.flags & flag::shared)
            {
                clocked->insert(new entry {
                    .startp = ent.startp,
                    .endp = ent.endp,
                    .obj = ent.obj,
                    .offp = ent.offp,
                    .amap = nullptr,
                    .anon_idx = 0,
                    .prot = ent.prot,
                    .max_prot = ent.max_prot,
                    .flags = ent.flags,
                    .hook = { },
                    .interval = { }
                });
                continue;
            }

            anon_map::ptr camap;
            if (ent.amap)
            {
                const auto span = ent.endp - ent.startp;

                camap = new anon_map { };
                camap->nslots = span;
                camap->slots = std::make_unique<anon::ptr[]>(camap->nslots);

                const std::unique_lock _ { ent.amap->lock };
                for (std::size_t i = 0; i < span; i++)
                {
                    auto &slot = ent.amap->slots[ent.anon_idx + i];
                    if (!slot)
                        continue;

                    auto pg = slot->pg;
                    pg->ref();
                    camap->slots[i] = new anon {
                        .pg = pg,
                        .hook = { }
                    };
                }
            }

            clocked->insert(new entry {
                .startp = ent.startp,
                .endp = ent.endp,
                .obj = ent.obj,
                .offp = ent.offp,
                .amap = camap,
                .anon_idx = 0,
                .prot = ent.prot,
                .max_prot = ent.max_prot,
                .flags = ent.flags,
                .hook = { },
                .interval = { }
            });

            if (ent.prot & prot::write)
            {
                const auto vaddr = ent.startp * npsize;
                const auto length = (ent.endp - ent.startp) * npsize;

                const auto pflags = prot_to_pflags(ent.prot & ~prot::write);
                lib::unused(pmap->protect(vaddr, length, pflags, psize));
            }
        }

        return ret;
    }

    anon::~anon()
    {
        lib::bug_on(!pg);
        if (pg->unref())
        {
            const auto psize = default_page_size();
            const auto npsize = pagemap::from_page_size(psize);
            const auto num_alloc_pages = npsize / pmm::page_size;
            pmm::free(paddr_from(pg), num_alloc_pages);
        }
    }

    bool handle_pfault(pfault_state state)
    {
        if (!sched::is_running())
            return false;

        if (state.address < vmspace::mmap_min || state.address >= vmspace::vspace_top)
            return false;

        if (state.is_present && !state.is_write && !state.is_exec)
            return false;

        const auto thread = sched::current_thread();
        if (thread->is_kernel())
            return false;

        const auto proc = thread->proc;
        const auto &vmspace = proc->vmspace;

        const auto psize = default_page_size();
        const auto npsize = pagemap::from_page_size(psize);
        const auto num_alloc_pages = npsize / pmm::page_size;

        const auto aligned = lib::align_down(state.address, npsize);

        object::ptr obj;
        anon_map::ptr amap;

        prot_t prot;
        flag_t flags;
        std::uintptr_t startp;
        std::uint64_t obj_offp;
        std::uint64_t anon_idx;

        {
            const auto locked = vmspace->tree.lock();
            const auto ret = locked->overlapping(aligned / npsize, (aligned / npsize) + 1);
            if (ret.empty())
                return false;

            auto &entry = ret.front();

            prot = entry.prot;
            flags = entry.flags;
            startp = entry.startp;

            if (entry.obj)
            {
                obj = entry.obj;
                obj_offp = entry.offp;
            }

            if (entry.amap)
            {
                amap = entry.amap;
                anon_idx = entry.anon_idx;
            }
        }

        if (!obj && !amap)
            return false;

        if (!(prot & prot::read))
            return false;

        if (state.is_write && !(prot & prot::write))
            return false;

        if (state.is_exec && !(prot & prot::exec))
            return false;

        const auto offp = (aligned / npsize) - startp;
        std::uintptr_t paddr = 0;

        page *pinned = nullptr;
        const auto check_pinned = [&] {
            if (pinned && pinned->unref())
                pmm::free(paddr_from(pinned), num_alloc_pages);
        };

        const auto copy_old = [&](page *opg, anon::ptr &slot, bool is_file) {
            paddr = pmm::alloc(num_alloc_pages, false);
            if (paddr == 0)
                return false;

            auto pg = page_for(paddr);
            pg->refcount.store(1, std::memory_order_relaxed);
            pg->flags.store(
                page::flag::anonymous | (is_file ? page::flag::dirty : 0),
                std::memory_order_relaxed
            );

            const auto old_vaddr = lib::tohh(paddr_from(opg));
            const auto new_vaddr = lib::tohh(paddr);

            std::memcpy(
                reinterpret_cast<void *>(new_vaddr),
                reinterpret_cast<void *>(old_vaddr),
                npsize
            );

            slot = pg->anon_ptr = new anon {
                .pg = pg,
                .hook = { }
            };

            return true;
        };

        if (flags & flag::anonymous)
        {
            if (flags & flag::private_)
            {
                lib::bug_on(!amap);
                const std::unique_lock _ { amap->lock };

                if (auto &slot = amap->slots[anon_idx + offp])
                {
                    auto opg = slot->pg;
                    if (state.is_write && opg->refcount.load(std::memory_order_acquire) != 1)
                    {
                        if (!copy_old(opg, slot, false))
                            return false;
                        goto end;
                    }

                    opg->ref();
                    pinned = opg;
                    paddr = paddr_from(opg);
                }
                else // not present or not in anon
                {
                    paddr = pmm::alloc(num_alloc_pages, true);
                    if (paddr == 0)
                        return false;

                    auto pg = page_for(paddr);
                    pg->refcount.store(1, std::memory_order_relaxed);
                    pg->flags.store(page::flag::anonymous, std::memory_order_relaxed);

                    amap->slots[anon_idx + offp] = pg->anon_ptr = new anon {
                        .pg = pg,
                        .hook = { }
                    };
                }
            }
            else // shared
            {
                lib::bug_on(!(flags & flag::shared));
                lib::bug_on(!obj);

                page *fetched = nullptr;
                // memobject
                const auto ret = obj->read_pages(obj_offp + offp, { &fetched, 1 }, 0);
                if (!ret || !fetched)
                    return false;

                pinned = fetched;
                paddr = paddr_from(fetched);
            }
        }
        else // file
        {
            if (flags & flag::private_)
            {
                lib::bug_on(!amap);
                const std::unique_lock alock { amap->lock };

                if (!state.is_present)
                {
                    page *opg = nullptr;
                    if (!amap->slots[anon_idx + offp])
                    {
                        lib::bug_on(!obj);
                        const auto ret = obj->read_pages(obj_offp + offp, { &opg, 1 }, 0);
                        if (!ret || !opg)
                            return false;
                    }
                    else
                    {
                        opg = amap->slots[anon_idx + offp]->pg;
                        opg->ref();
                    }

                    if (!state.is_write)
                    {
                        pinned = opg;
                        paddr = paddr_from(opg);
                    }
                    else
                    {
                        if (!copy_old(opg, amap->slots[anon_idx + offp], true))
                        {
                            if (opg->unref())
                                pmm::free(paddr_from(opg), num_alloc_pages);
                            return false;
                        }
                        pinned = opg;
                    }
                }
                else // write
                {
                    page *opg = nullptr;
                    if (amap->slots[anon_idx + offp])
                    {
                        opg = amap->slots[anon_idx + offp]->pg;
                        if (opg->refcount.load(std::memory_order_acquire) == 1)
                        {
                            opg->ref();
                            pinned = opg;
                            paddr = paddr_from(opg);
                            goto end;
                        }
                        opg->ref();
                    }
                    else
                    {
                        lib::bug_on(!obj);
                        const auto ret = obj->read_pages(obj_offp + offp, { &opg, 1 }, 0);
                        if (!ret || !opg)
                            return false;
                    }

                    if (!copy_old(opg, amap->slots[anon_idx + offp], true))
                    {
                        if (opg->unref())
                            pmm::free(paddr_from(opg), num_alloc_pages);
                        return false;
                    }
                    pinned = opg;
                }
            }
            else // shared
            {
                lib::bug_on(!obj);

                page *fetched = nullptr;
                const auto ret = obj->read_pages(obj_offp + offp, { &fetched, 1 }, 0);
                if (!ret || !fetched)
                    return false;

                pinned = fetched;
                paddr = paddr_from(fetched);

                if (state.is_write)
                    fetched->flags.fetch_or(page::flag::dirty, std::memory_order_relaxed);
            }
        }

        end:

        {
            const auto locked = vmspace->tree.lock();
            const auto ret = locked->overlapping(aligned / npsize, (aligned / npsize) + 1);

            if (ret.empty())
                goto fail;

            auto &entry = ret.front();

            if (entry.flags != flags)
                goto fail;

            if (!(entry.prot & prot::read))
                goto fail;
            if (state.is_write && !(entry.prot & prot::write))
                goto fail;
            if (state.is_exec && !(entry.prot & prot::exec))
                goto fail;

            const auto new_offp = (aligned / npsize) - entry.startp;

            if (obj && (entry.obj != obj || (entry.offp + new_offp) != (obj_offp + offp)))
                goto fail;
            if (amap && (entry.amap != amap || (entry.anon_idx + new_offp) != (anon_idx + offp)))
                goto fail;

            if ((flags & flag::private_) && (entry.prot & prot::write) && !state.is_write)
                prot &= ~prot::write;
        }

        if (vmspace->pmap->map(aligned, paddr, npsize, prot_to_pflags(prot), psize))
        {
            if (state.is_present && state.is_write)
                vmspace->pmap->invalidate(aligned, npsize);
            check_pinned();
            return true;
        }

        fail:
        check_pinned();
        return false;
    }
} // namespace vmm
