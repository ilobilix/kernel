// Copyright (C) 2024-2026  ilobilo

module system.memory.virt;

import system.memory.phys;
import system.scheduler;
import system.cpu;
import magic_enum;
import lib;
import std;

import :pagemap;

namespace vmm
{
    using namespace magic_enum::bitwise_operators;

    namespace
    {
        constexpr auto def_psize = page_size::small;

        constexpr std::size_t num_waitqueues = 256;
        lib::wait_queue waitqueues[num_waitqueues];

        std::size_t hash_page(page *pg)
        {
            const auto addr = reinterpret_cast<std::uintptr_t>(pg);
            return (addr >> std::countr_zero(sizeof(page))) % num_waitqueues;
        }

        void wait_on_busy_page(page *pg)
        {
            const auto me = sched::this_thread();
            lib::wait_queue_entry wqe { me };

            auto &wq = waitqueues[hash_page(pg)];
            while (pg->flags.load(std::memory_order_acquire) & page::flag::busy)
            {
                me->prepare_sleep();
                wq.add(&wqe);

                if (!(pg->flags.load(std::memory_order_acquire) & page::flag::busy))
                {
                    wq.remove(&wqe);
                    me->cancel_sleep();
                    break;
                }

                sched::yield();
                wq.remove(&wqe);
            }
        }

        std::uintptr_t pfndb_base()
        {
            // doesn't change
            static const auto cached = [] { return pmm::info().pfndb_base; } ();
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
    } // namespace

    std::size_t default_page_size()
    {
        return pagemap::from_page_size(def_psize);
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

    lib::expect<void> object::read_pages(
        std::uint64_t offp, std::span<page *> pages, std::size_t idx
    )
    {
        lib::bug_on(pages.size() > max_readahead);

        const auto psize = default_page_size();
        const auto num_alloc_pages = psize / pmm::page_size;

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
                pg->flags.store(type | page::flag::busy, std::memory_order_relaxed);

                locked->insert({ start_idx + i, pg });
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
        const auto num_alloc_pages = psize / pmm::page_size;

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
        const auto num_alloc_pages = psize / pmm::page_size;

        std::size_t progress = 0;
        std::size_t remaining = size;

        std::size_t curr_idx = offset / psize;
        std::size_t poffset = offset % psize;

        page *chunk[max_readahead];

        while (remaining > 0)
        {
            const auto num_pages = std::min(
                (poffset + remaining + psize - 1) / psize,
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
                const auto copy_len = std::min(psize - poffset, remaining);

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

    lib::expect<std::uintptr_t> vmspace::map(
        std::uintptr_t hint, std::size_t length,
        prot_t prot, prot_t max_prot, flag_t flags,
        object::ptr obj, std::uint64_t offset
    )
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        if (!((flags & flag::shared) ^ (flags & flag::private_)))
            return std::unexpected { lib::err::invalid_flags };

        if ((max_prot & prot) != prot)
            return std::unexpected { lib::err::permission_denied };

        const auto psize = default_page_size();
        if (obj && offset % psize)
            return std::unexpected { lib::err::addr_not_aligned };

        const auto offp = offset / psize;

        length = lib::align_up(length, psize);

        object::ptr target_obj;
        anon_map::ptr target_amap;

        const bool is_anon = flags & flag::anonymous;
        const bool is_file = !is_anon;

        if (is_file)
            target_obj = std::move(obj);

        if (flags & flag::private_)
        {
            target_amap = new anon_map { };
            target_amap->nslots = length / psize;
            target_amap->slots = std::make_unique<anon::ptr []>(target_amap->nslots);
        }
        else if ((flags & flag::shared) && (flags & flag::anonymous))
            target_obj = new memobject { };

        std::uintptr_t startp = 0;
        std::uintptr_t endp = 0;

        auto wlocked = tree.write_lock();

        if ((flags & flag::fixed) || (flags & flag::fixed_noreplace))
        {
            if (hint % psize)
                return std::unexpected { lib::err::addr_not_aligned };

            if (hint < mmap_min || hint + length > vspace_top)
                return std::unexpected { lib::err::addr_out_of_bounds };

            startp = hint / psize;
            endp = (hint + length) / psize;

            {
                const auto overlapping = wlocked->overlapping(startp, endp);
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
                const auto overlapping = wlocked->overlapping(startp, endp);

                auto it = overlapping.begin();
                if (it == overlapping.end())
                    break;

                auto ent = it.value();

                const auto overlap_start = std::max(startp, ent->startp);
                const auto overlap_end = std::min(endp, ent->endp);

                const auto unmap_vaddr = overlap_start * psize;
                const auto unmap_length = (overlap_end - overlap_start) * psize;

                if (const auto ret = pmap->unmap(unmap_vaddr, unmap_length); !ret)
                    return std::unexpected { ret.error() };

                wlocked->remove(ent);

                if (ent->endp > endp)
                {
                    const auto pages = endp - ent->startp;

                    const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                    const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;

                    wlocked->insert(new entry {
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
                    wlocked->insert(ent);
                }
                else delete ent;
            }
        }
        else
        {
            const auto ret = find_free_region_internal(wlocked, length);
            if (!ret)
                return std::unexpected { lib::err::out_of_memory };

            startp = *ret / psize;
            endp = (*ret + length) / psize;
        }

        wlocked->insert(new entry {
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

        return startp * psize;
    }

    lib::expect<void> vmspace::unmap(std::uintptr_t address, std::size_t length)
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        const auto psize = default_page_size();
        if (address % psize)
            return std::unexpected { lib::err::addr_not_aligned };

        length = lib::align_up(length, psize);

        if (address < mmap_min || address + length > vspace_top)
            return std::unexpected { lib::err::addr_out_of_bounds };

        const auto startp = address / psize;
        const auto endp = (address + length) / psize;

        auto wlocked = tree.write_lock();

        {
            const auto overlapping = wlocked->overlapping(startp, endp);

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
            }
        }

        while (true)
        {
            const auto overlapping = wlocked->overlapping(startp, endp);

            auto it = overlapping.begin();
            if (it == overlapping.end())
                break;

            auto ent = it.value();

            const auto overlap_start = std::max(startp, ent->startp);
            const auto overlap_end = std::min(endp, ent->endp);

            const auto unmap_vaddr = overlap_start * psize;
            const auto unmap_length = (overlap_end - overlap_start) * psize;

            if (const auto ret = pmap->unmap(unmap_vaddr, unmap_length); !ret)
                return std::unexpected { ret.error() };

            wlocked->remove(ent);

            if (ent->endp > overlap_end)
            {
                const auto pages = overlap_end - ent->startp;

                const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;

                wlocked->insert(new entry {
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
                wlocked->insert(new entry {
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
        if (address % psize)
            return std::unexpected { lib::err::addr_not_aligned };

        length = lib::align_up(length, psize);

        if (address < mmap_min || address + length > vspace_top)
            return std::unexpected { lib::err::addr_out_of_bounds };

        const auto startp = address / psize;
        const auto endp = (address + length) / psize;

        auto wlocked = tree.write_lock();

        {
            const auto overlapping = wlocked->overlapping(startp, endp);

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

        while (true)
        {
            const auto overlapping = wlocked->overlapping(startp, endp);

            auto it = overlapping.begin();
            if (it == overlapping.end())
                break;

            auto ent = it.value();

            const auto overlap_start = std::max(startp, ent->startp);
            const auto overlap_end = std::min(endp, ent->endp);

            wlocked->remove(ent);

            if (ent->endp > overlap_end)
            {
                const auto pages = overlap_end - ent->startp;

                const auto obj_offp = ent->obj ? ent->offp + pages : 0;
                const auto anon_idx = ent->amap ? ent->anon_idx + pages : 0;

                wlocked->insert(new entry {
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
                wlocked->insert(new entry {
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

            wlocked->insert(ent);
        }

        if (const auto ret = pmap->protect(address, length, prot_to_pflags(prot)); !ret)
            return std::unexpected { ret.error() };

        return { };
    }

    lib::expect<std::uintptr_t> vmspace::find_free_region_internal(auto &locked, std::size_t length)
    {
        if (locked->empty())
        {
            if (length > mmap_max - mmap_min)
                return std::unexpected { lib::err::out_of_memory };

            return mmap_max - length;
        }

        const auto root = locked->root();

        const auto max_gap = root->interval.subtree_max_gap;
        const auto high_gap = mmap_max - root->interval.subtree_max;
        const auto low_gap = root->interval.subtree_min - mmap_min;

        if (max_gap < length && high_gap < length && low_gap < length)
            return std::unexpected { lib::err::out_of_memory };

        if (high_gap >= length)
            return mmap_max - length;

        const auto nil = locked->nil();
        auto node = locked->root();

        std::uintptr_t floor = mmap_min;
        while (node)
        {
            const auto left = node->hook.left;
            const auto right = node->hook.right;

            if (right != nil && right->interval.subtree_max_gap >= length)
            {
                auto current_max = node->endp;
                if (left != nil && left->interval.subtree_max > current_max)
                    current_max = left->interval.subtree_max;

                if (current_max > floor)
                    floor = current_max;

                node = right;
                continue;
            }

            const auto prev_endp = (left != nil ? left->interval.subtree_max : floor);
            if (node->startp > prev_endp && (node->startp - prev_endp) >= length)
                return node->startp - length;

            if (left == nil)
                break;

            node = left;
        }

        const auto first = locked->first();
        if (first->startp - mmap_min >= length)
            return first->startp - length;

        return std::unexpected { lib::err::out_of_memory };
    }

    lib::expect<std::uintptr_t> vmspace::find_free_region(std::size_t length)
    {
        if (length == 0)
            return std::unexpected { lib::err::invalid_length };

        const auto rlocked = tree.read_lock();
        return find_free_region_internal(rlocked, length);
    }

    anon::~anon()
    {
        lib::bug_on(!pg);
        if (pg->unref())
        {
            const auto psize = default_page_size();
            const auto num_alloc_pages = psize / pmm::page_size;
            pmm::free(paddr_from(pg), num_alloc_pages);
        }
    }

    bool handle_pfault(pfault_state state)
    {
        if (!sched::is_initialised())
            return false;

        if (state.address < vmspace::mmap_min || state.address >= vmspace::vspace_top)
            return false;

        if (state.is_present && !state.is_write && !state.is_exec)
            return false;

        const auto thread = sched::this_thread();
        if (!thread->is_user)
            return false;

        const auto proc = thread->parent;
        const auto &vmspace = proc->vmspace;

        const auto psize = default_page_size();
        const auto num_alloc_pages = psize / pmm::page_size;

        object::ptr obj;
        anon_map::ptr amap;

        prot_t prot;
        flag_t flags;
        std::uintptr_t startp;
        std::uint64_t obj_offp;
        std::uint64_t anon_idx;

        const auto find_cmp = [&](const auto &ent) {
            if ((ent.startp * psize) <= state.address && state.address < (ent.endp * psize))
                return 0;
            return (ent.startp * psize) < state.address ? -1 : 1;
        };

        {
            const auto rlocked = vmspace->tree.read_lock();
            const auto entry = rlocked->find(find_cmp);

            if (!entry)
                return false;

            prot = entry->prot;
            flags = entry->flags;
            startp = entry->startp;

            if (entry->obj)
            {
                obj = entry->obj;
                obj_offp = entry->offp;
            }

            if (entry->amap)
            {
                amap = entry->amap;
                anon_idx = entry->anon_idx;
            }
        }

        if (!obj && !amap)
            return false;

        if (state.is_write && !(prot & prot::write))
            return false;

        if (state.is_exec && !(prot & prot::exec))
            return false;

        const auto aligned = lib::align_down(state.address, psize);
        const auto offp = (aligned / psize) - startp;
        std::uintptr_t paddr = 0;

        page *pinned = nullptr;
        const auto check_pinned = [&] {
            if (pinned && pinned->unref())
                pmm::free(paddr_from(pinned), num_alloc_pages);
        };

        if (flags & flag::anonymous)
        {
            if (flags & flag::private_)
            {
                lib::bug_on(!amap);
                std::unique_lock _ { amap->lock };

                if (auto &slot = amap->slots[anon_idx + offp])
                {
                    auto opg = slot->pg;
                    opg->ref();

                    if (state.is_write)
                    {
                        if (opg->refcount.load(std::memory_order_acquire) != 1)
                        {
                            // cow
                            paddr = pmm::alloc(num_alloc_pages, false);
                            if (paddr == 0)
                                return false;

                            auto pg = page_for(paddr);
                            pg->refcount.store(1, std::memory_order_relaxed);
                            pg->flags.store(page::flag::anonymous, std::memory_order_relaxed);

                            const auto old_vaddr = lib::tohh(paddr_from(opg));
                            const auto new_vaddr = lib::tohh(paddr);

                            std::memcpy(
                                reinterpret_cast<void *>(new_vaddr),
                                reinterpret_cast<void *>(old_vaddr),
                                psize
                            );

                            if (opg->unref())
                                pmm::free(paddr_from(opg), num_alloc_pages);

                            slot = new anon {
                                .pg = pg,
                                .hook { }
                            };

                            goto end;
                        }
                    }

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

                    amap->slots[anon_idx + offp] = new anon {
                        .pg = pg,
                        .hook { }
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
                std::unique_lock _ { amap->lock };

                if (!state.is_present)
                {
                    if (!amap->slots[anon_idx + offp])
                    {
                        lib::bug_on(!obj);

                        page *fetched = nullptr;
                        const auto ret = obj->read_pages(obj_offp + offp, { &fetched, 1 }, 0);
                        if (!ret || !fetched)
                            return false;

                        pinned = fetched;
                        paddr = paddr_from(fetched);
                    }
                    else
                    {
                        auto opg = amap->slots[anon_idx + offp]->pg;
                        opg->ref();
                        pinned = opg;
                        paddr = paddr_from(opg);
                    }
                    prot &= ~prot::write;
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
                    }
                    else
                    {
                        lib::bug_on(!obj);
                        const auto ret = obj->read_pages(obj_offp + offp, { &opg, 1 }, 0);
                        if (!ret || !opg)
                            return false;
                    }

                    paddr = pmm::alloc(num_alloc_pages, true);
                    if (paddr == 0)
                        return false;

                    auto pg = page_for(paddr);
                    pg->refcount.store(1, std::memory_order_relaxed);
                    pg->flags.store(
                        page::flag::anonymous | page::flag::dirty,
                        std::memory_order_relaxed
                    );

                    const auto old_vaddr = lib::tohh(paddr_from(opg));
                    const auto new_vaddr = lib::tohh(paddr);

                    std::memcpy(
                        reinterpret_cast<void *>(new_vaddr),
                        reinterpret_cast<void *>(old_vaddr),
                        psize
                    );

                    if (opg->unref())
                        pmm::free(paddr_from(opg), num_alloc_pages);

                    amap->slots[anon_idx + offp] = new anon {
                        .pg = pg,
                        .hook { }
                    };
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
            const auto rlocked = vmspace->tree.read_lock();
            const auto entry = rlocked->find(find_cmp);

            if (!entry)
                goto fail;

            if (state.is_write && !(entry->prot & prot::write))
                goto fail;
            if (state.is_exec && !(entry->prot & prot::exec))
                goto fail;

            const auto new_offp = (aligned / psize) - entry->startp;

            if (obj && (entry->obj != obj || (entry->offp + new_offp) != (obj_offp + offp)))
                goto fail;
            if (amap && (entry->amap != amap || (entry->anon_idx + new_offp) != (anon_idx + offp)))
                goto fail;
        }

        // TODO: tlb shootdown if (state.is_present && state.is_write)
        if (vmspace->pmap->map(aligned, paddr, psize, prot_to_pflags(prot)))
        {
            check_pinned();
            return true;
        }

        fail:
        check_pinned();
        return false;
    }
} // namespace vmm
