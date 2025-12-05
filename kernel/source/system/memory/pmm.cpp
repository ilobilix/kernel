// Copyright (C) 2024-2025  ilobilo

module system.memory.phys;

import drivers.initramfs;
import system.memory.virt;
import system.scheduler;
import magic_enum;
import frigg;
import boot;
import lib;
import std;

namespace pmm
{
    namespace
    {
        constinit lib::spinlock_irq lock;
        constinit memory mem;
        constinit bool initialised = false;

        void *bootstrap_alloc(std::size_t npages);

        template<std::uintptr_t Start, std::uintptr_t End>
        struct allocator
        {
            static inline constexpr std::uintptr_t start = Start;
            static inline constexpr std::uintptr_t end = End;

            struct list { list *prev = nullptr; list *next = nullptr; };
            list lists[max_order + 1];

            constexpr allocator() : lists { } { }

            inline std::ptrdiff_t prev_order_from(std::size_t size)
            {
                if (size < page_size)
                    return -1;
                return lib::log2(lib::pre_pow2(size)) - 12;
            }

            inline std::ptrdiff_t next_order_from(std::size_t size)
            {
                if (size < page_size)
                    return -1;
                return lib::log2(lib::next_pow2(size)) - 12;
            }

            inline void put(std::size_t order, list *pg)
            {
                pg->next = lists[order].next;
                pg->prev = nullptr;
                if (lists[order].next)
                    lists[order].next->prev = pg;
                lists[order].next = pg;
            }

            inline void *rem(std::size_t order)
            {
                const auto ret = lists[order].next;
                lists[order].next = lists[order].next->next;
                if (lists[order].next)
                    lists[order].next->prev = nullptr;
                return ret;
            }

            inline bool has_pages(std::size_t order)
            {
                return lists[order].next != nullptr;
            }

            inline bool in_range(const auto ptr)
            {
                const auto addr = lib::fromhh(reinterpret_cast<std::uintptr_t>(ptr));
                return addr >= start && addr < end;
            }

            inline bool in_range(std::uintptr_t rstart, std::uintptr_t rend)
            {
                return lib::range_overlaps(rstart, rend, start, end);
            }

            inline std::pair<std::uintptr_t, std::uintptr_t> range_intersection(std::uintptr_t rstart, std::uintptr_t rend)
            {
                return lib::range_intersection(rstart, rend, start, end);
            }

            std::size_t add_range(std::uintptr_t base, std::size_t size)
            {
                if (base < start || base + size > end)
                    return size;

                base = lib::tohh(base);

                while (size >= page_size)
                {
                    const auto pg = std::construct_at<list>(reinterpret_cast<list *>(base));
                    auto sorder = prev_order_from(size);
                    if (sorder < 0)
                        break;

                    auto order = std::min(static_cast<std::size_t>(sorder), max_order);
                    while (base % (page_size * lib::pow2(order)))
                    {
                        order--;
                        if (order == static_cast<std::size_t>(-1))
                            break;
                    }

                    if (order == static_cast<std::size_t>(-1))
                        break;

                    const auto pg_page = page_for(base);
                    pg_page->order = order;

                    put(order, pg);

                    const auto offset = page_size * lib::pow2(order);
                    size -= offset;
                    base += offset;
                }

                return size;
            }

            void split_to(std::size_t target)
            {
                auto current = target + 1;
                while (lists[current].next == nullptr)
                {
                    current++;
                    if (current > max_order)
                        return;
                }

                while (current > target)
                {
                    const auto pg = reinterpret_cast<list *>(rem(current));
                    const auto pgaddr = reinterpret_cast<std::uintptr_t>(pg);

                    current--;

                    const auto buddy = reinterpret_cast<list *>(pgaddr + page_size * lib::pow2(current));

                    const auto pg_page = page_for(pgaddr);
                    const auto buddy_page = page_for(reinterpret_cast<std::uintptr_t>(buddy));

                    lib::bug_on(pg_page->allocated != 0);
                    lib::bug_on(buddy_page->allocated != 0);

                    pg_page->order = current;
                    buddy_page->order = current;

                    buddy_page->next_paddr = pg_page->next_paddr;
                    pg_page->next_paddr = lib::fromhh(reinterpret_cast<std::uintptr_t>(buddy)) >> page_bits;

                    put(current, pg);
                    put(current, buddy);
                }
            }

            void coalesce_to(std::size_t target)
            {
                if (target == 0)
                    return;

                if (target > max_order)
                    target = max_order;

                for (std::size_t order = 0; order < target; order++)
                {
                    const auto next_block_size = page_size * lib::pow2(order + 1);
                    auto curr = lists[order].next;

                    while (has_pages(order))
                    {
                        while (!page_for(curr)->next_paddr || reinterpret_cast<std::uintptr_t>(curr) % next_block_size)
                        {
                            curr = curr->next;
                            if (!curr)
                                goto end;
                        }

                        const auto curr_page = page_for(curr);
                        lib::bug_on(curr_page->allocated != 0);
                        lib::bug_on(curr_page->order != order);

                        const auto buddy_addr = lib::tohh(curr_page->next_paddr << page_bits);
                        const auto buddy_page = page_for(buddy_addr);

                        if (buddy_page->allocated || buddy_page->order != order)
                        {
                            // cannot coalesce :(
                            curr = curr->next;
                            if (!curr)
                                goto end;
                            continue;
                        }

                        auto remove = [this, &order](auto pg)
                        {
                            if (pg->prev)
                                pg->prev->next = pg->next;
                            else
                                lists[order].next = pg->next;

                            if (pg->next)
                                pg->next->prev = pg->prev;
                        };

                        remove(curr);
                        remove(reinterpret_cast<list *>(buddy_addr));

                        const auto merged = reinterpret_cast<list *>(curr);
                        const auto merged_page = page_for(reinterpret_cast<std::uintptr_t>(merged));
                        merged_page->order = order + 1;
                        merged_page->next_paddr = buddy_page->next_paddr;

                        put(order + 1, merged);

                        curr = lists[order].next;
                    }
                    end:
                }
            }

            std::size_t free(void *ptr, std::size_t npages)
            {
                const auto size = npages * page_size;
                const auto order = next_order_from(size);
                if (order < 0 || static_cast<std::size_t>(order) > max_order)
                    return 0;

                const auto pg = page_for(reinterpret_cast<std::uintptr_t>(ptr));
                lib::bug_on(pg->allocated == 0);
                lib::bug_on(pg->order != order);
                pg->allocated = 0;

                put(order, static_cast<list *>(lib::tohh(ptr)));
                return size;
            }

            std::pair<void *, std::size_t> alloc(std::size_t npages)
            {
                const auto size = npages * page_size;
                const auto order = next_order_from(size);
                const auto real_size = page_size * lib::pow2(order);

                if (order < 0 || static_cast<std::size_t>(order) > max_order)
                    return { nullptr, 0 };

                if (!has_pages(order))
                {
                    if (order == max_order)
                    {
                        coalesce_to(max_order);
                        if (!has_pages(order))
                            return { nullptr, 0 };
                        goto found;
                    }
                    split_to(order);
                    if (!has_pages(order))
                    {
                        if (order != 0)
                        {
                            coalesce_to(order);
                            if (!has_pages(order))
                                return { nullptr, 0 };
                            goto found;
                        }
                        return { nullptr, 0 };
                    }
                }
                found:
                const auto ret = rem(order);

                const auto pg = page_for(reinterpret_cast<std::uintptr_t>(ret));
                lib::bug_on(pg->allocated == 1);
                lib::bug_on(pg->order != order);
                pg->allocated = 1;

                return { ret, real_size };
            }
        };

        constinit allocator<page_size, lib::mib(1)> sub1mib { };
        constinit allocator<lib::mib(1), lib::gib(4)> sub4gib { };
        constinit allocator<lib::gib(4), (1ul << paddr_bits)> normal { };

        void add_range(std::uintptr_t base, std::size_t size, bool freeing)
        {
            if (size == 0)
                return;

            if (!freeing)
            {
                mem.usable += size;
                if (size < page_size)
                {
                    mem.used += size;
                    return;
                }

                if (base == 0)
                {
                    if (size < page_size * 2)
                        return;
                    base += page_size;
                    size -= page_size;
                    mem.used += page_size;
                }
            }
            else if (base == 0)
            {
                if (size < page_size * 2)
                    return;
                base += page_size;
                size -= page_size;
            }

            std::size_t wasted = 0;
            const auto check_and_add = [=, &wasted](auto &alloc)
            {
                if (const auto [s, e] = alloc.range_intersection(base, base + size); s < e)
                {
                    const auto ret = alloc.add_range(s, e - s);
                    lib::bug_on(ret == e - s);
                    wasted += ret;
                }
            };

            check_and_add(sub1mib);
            check_and_add(sub4gib);
            check_and_add(normal);

            if (!freeing)
                mem.used += wasted;
            else
                mem.used -= size - wasted;
            return;
        }

        std::size_t bootstrap_memmap_idx = -1;
        boot::limine_memmap_entry bootstrap_memmap;

        void *bootstrap_alloc(std::size_t npages)
        {
            lib::bug_on(npages != 1);

            // first called when allocating the pagemap
            [[maybe_unused]]
            static const auto once = [] {
                lib::info("pmm: setting up bootstrap allocator");

                const auto memmaps = boot::requests::memmap.response->entries;
                const std::size_t num = boot::requests::memmap.response->entry_count;

                std::size_t max_size = 0, idx = -1;
                for (std::size_t i = 0; i < num; i++)
                {
                    const auto memmap = memmaps[i];
                    if (static_cast<boot::memmap>(memmap->type) != boot::memmap::usable)
                        continue;

                    if (memmap->length > max_size)
                    {
                        max_size = memmap->length;
                        idx = i;
                    }
                }
                lib::bug_on(max_size == 0 || idx == static_cast<std::size_t>(-1));

                bootstrap_memmap_idx = idx;
                bootstrap_memmap = *memmaps[idx];
                bootstrap_memmap.length = lib::align_down(bootstrap_memmap.length, page_size);

                return true;
            } ();

            if (bootstrap_memmap.length < npages * page_size)
                lib::panic("pmm: bootstrap allocator is out of memory");

            const auto ret = bootstrap_memmap.base + bootstrap_memmap.length - npages * page_size;
#if defined(__x86_64__)
            if (ret < lib::mib(1))
                lib::panic("pmm: bootstrap allocator tried to allocate memory below 1 MiB");
#endif
            bootstrap_memmap.length -= npages * page_size;

            return reinterpret_cast<void *>(lib::tohh(ret));
        }

        void pfndb_add(std::size_t idx)
        {
            const auto memmaps = boot::requests::memmap.response->entries;
            const auto memmap = memmaps[idx];

            const auto pg = reinterpret_cast<std::uintptr_t>(page_for(memmap->base));
            const auto vstart = lib::align_down(pg, page_size);

            // why doesn't this map enough memory without + page_size if struct page size is a multiple of 16 bytes?
            const auto max_length = lib::align_up(memmap->length / page_size * sizeof(page), page_size) + page_size;

            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::rwg;

            for (std::size_t vaddr = vstart; vaddr < vstart + max_length; vaddr += page_size)
            {
                if (idx == bootstrap_memmap_idx && bootstrap_memmap.length < page_size * 5 /* 5? */)
                    lib::panic("pmm: could not map pfndb: out of memory");

                if (const auto ret = vmm::kernel_pagemap->translate(vaddr, psize); ret && ret.value() != 0)
                    continue;

                const auto paddr = alloc<std::uintptr_t>(1, true);
                if (const auto ret = vmm::kernel_pagemap->map(vaddr, paddr, page_size, flags, psize); !ret)
                    lib::panic("pmm: could not map pfndb: {}", magic_enum::enum_name(ret.error()));
            }
        };
    } // namespace

    memory info()
    {
        const std::unique_lock _ { lock };
        return mem;
    }

    page *page_for(std::uintptr_t addr)
    {
        const auto idx = lib::fromhh(addr) / page_size;
        const auto pg = reinterpret_cast<page *>(mem.pfndb_base + idx * sizeof(page));
        return pg;
    }

    [[nodiscard]]
    void *alloc(std::size_t npages, bool clear, type tp)
    {
        if (npages == 0)
            return nullptr;

        const std::unique_lock _ { lock };
        const auto size = npages * page_size;

        std::pair<void *, std::size_t> ret { nullptr, 0 };
        if (initialised)
        {
            switch (tp)
            {
                case type::normal:
                    ret = normal.alloc(npages);
                    if (!ret.first && bootstrap_memmap_idx != static_cast<std::size_t>(-1))
                        ret = { bootstrap_alloc(npages), size };
                    if (!ret.first)
                        ret = sub4gib.alloc(npages);
#if !defined(__x86_64__)
                    if (!ret.first)
                        ret = sub1mib.alloc(npages);
#endif
                    break;
                case type::sub4gib:
                    ret = sub4gib.alloc(npages);
                    break;
                case type::sub1mib:
                    ret = sub1mib.alloc(npages);
                    break;
                default:
                    lib::panic("pmm: unknown allocation type {}", magic_enum::enum_name(tp));
            }
        }
        else ret = { bootstrap_alloc(npages), size };

        if (!ret.first)
        {
            lib::panic(
                "pmm: could not allocate {} page{}. type: {}",
                npages, npages == 1 ? "" : "s", magic_enum::enum_name(tp)
            );
        }

        if (clear)
            std::memset(ret.first, 0, size);

        mem.used += ret.second;
        return lib::fromhh(ret.first);
    }

    void free(void *ptr, std::size_t npages)
    {
        if (npages == 0 || ptr == nullptr)
            return;

        if (initialised)
        {
            const std::unique_lock _ { lock };

            if (sub1mib.in_range(ptr))
                mem.used -= sub1mib.free(ptr, npages);
            else if (sub4gib.in_range(ptr))
                mem.used -= sub4gib.free(ptr, npages);
            else if (normal.in_range(ptr))
                mem.used -= normal.free(ptr, npages);
            else
                lib::panic("pmm: attempted to free memory outside managed ranges: 0x{:X}", reinterpret_cast<std::uintptr_t>(ptr));
        }
        else lib::panic("pmm: attempted to free bootstrap memory");
    }

    void reclaim_bootloader_memory()
    {
        lib::debug("pmm: reclaiming bootloader memory");

        const auto memmaps = boot::requests::memmap.response->entries;
        const std::size_t num = boot::requests::memmap.response->entry_count;

        for (std::size_t i = 0; i < num; i++)
        {
            const auto memmap = memmaps[i];
            if (static_cast<boot::memmap>(memmap->type) != boot::memmap::bootloader)
                continue;

            add_range(memmap->base, memmap->length, true);
        }
    }

    void init()
    {
        const auto memmaps = boot::requests::memmap.response->entries;
        const std::size_t num = boot::requests::memmap.response->entry_count;

        lib::debug("pmm: number of memory maps: {}", num);

        for (std::size_t i = 0; i < num; i++)
        {
            const auto memmap = memmaps[i];
            const std::uintptr_t end = memmap->base + memmap->length;
            mem.top = std::max(mem.top, end);

            if (static_cast<boot::memmap>(memmap->type) == boot::memmap::usable)
                mem.usable_top = std::max(mem.usable_top, end);
        }

        std::size_t pfndb_used_total = 0;
        {
            lib::info("pmm: setting up pfndb");

            mem.pfndb_base = lib::tohh(lib::align_up(mem.top, lib::gib(1)));
            lib::debug("pmm: pfndb base: 0x{:X}", mem.pfndb_base);

            const std::size_t num_pages = lib::div_roundup(mem.usable_top, page_size);
            mem.pfndb_end = mem.pfndb_base + num_pages * sizeof(page);

            std::size_t start = mem.used;

            for (std::size_t i = 0; i < num; i++)
            {
                if (i == bootstrap_memmap_idx)
                    continue;

                const auto memmap = memmaps[i];
                const auto type = static_cast<boot::memmap>(memmap->type);

                if (type != boot::memmap::usable && type != boot::memmap::bootloader)
                    continue;

                pfndb_add(i);
            }
            pfndb_used_total += mem.used - start;
        }

        lib::info("pmm: initialising the physical memory allocator");

        for (std::size_t i = 0; i < num; i++)
        {
            if (i == bootstrap_memmap_idx)
                continue;

            const auto memmap = memmaps[i];

            const auto type = static_cast<boot::memmap>(memmap->type);
            if (type != boot::memmap::usable)
            {
                if (type == boot::memmap::kernel_and_modules || type == boot::memmap::bootloader)
                {
                    mem.used += memmap->length;
                    mem.usable += memmap->length;
                }
                continue;
            }

            add_range(memmap->base, memmap->length, false);
        }

        initialised = true;

        {
            lib::debug("pmm: adding bootstrap memory to pfndb");

            std::size_t start = mem.used;
            *memmaps[bootstrap_memmap_idx] = bootstrap_memmap;
            pfndb_add(bootstrap_memmap_idx);
            pfndb_used_total += mem.used - start;

            lib::debug("pmm: total memory used for pfndb: {} KiB", pfndb_used_total / lib::kib(1));
        }

        lib::debug("pmm: adding bootstrap memory to allocator");
        add_range(bootstrap_memmap.base, bootstrap_memmap.length, false);

        bootstrap_memmap_idx = -1;
    }
} // namespace pmm