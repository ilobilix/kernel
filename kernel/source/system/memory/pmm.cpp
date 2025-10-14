// Copyright (C) 2024-2025  ilobilo

module system.memory.phys;

import drivers.initramfs;
import system.memory.virt;
import system.scheduler;
import magic_enum;
import frigg;
import boot;
import lib;
import cppstd;

namespace pmm
{
#if defined(__x86_64__)
    extern "C" constinit std::uintptr_t trampoline_pages = 0;
#endif

    namespace
    {
        constinit lib::spinlock lock;
        constinit memory mem;
        constinit bool initialised = false;

#if defined(__x86_64__)
        constexpr std::size_t reserved_range_start = page_size;
        constexpr std::size_t reserved_range_end = lib::mib(1);
        // one page for trampoline and the other for temporary stack
        constexpr std::size_t requested_size = page_size * 2;
#endif

        void *bootstrap_alloc(std::size_t npages);

        struct allocator
        {
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

            std::size_t add_range(std::uintptr_t base, std::size_t size)
            {
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

        constinit allocator general { };

#if defined(__x86_64__)
        constinit allocator lowmem { };
        constexpr std::uintptr_t lowmem_end = lib::gib(4);

        inline bool is_lowmem(const auto ptr)
        {
            return lib::fromhh(reinterpret_cast<std::uintptr_t>(ptr)) < lowmem_end;
        }
#endif

        void add_range(std::uintptr_t base, std::size_t size, bool freeing)
        {
            if (!freeing)
                mem.usable += size;

            std::size_t wasted = 0;

#if defined(__x86_64__)
            if (is_lowmem(base))
            {
                const auto diff = lowmem_end - base;
                wasted = lowmem.add_range(base, std::min(size, diff));
                if (size > diff)
                    wasted += general.add_range(lowmem_end, size - diff);
            }
            else
#endif
                wasted = general.add_range(base, size);

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

            [[maybe_unused]]
            static const auto once = [] {
                const auto memmaps = boot::requests::memmap.response->entries;
                const std::size_t num = boot::requests::memmap.response->entry_count;

                std::size_t max_size = 0, idx = -1;
                for (std::size_t i = 0; i < num; i++)
                {
                    const auto memmap = memmaps[i];
                    if (static_cast<boot::memmap>(memmap->type) != boot::memmap::usable)
                        continue;

#if defined(__x86_64__)
                    const auto end = memmap->base + memmap->length;
                    if (reserved_range_start < end && memmap->base < reserved_range_end)
                        continue;
#endif

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
            bootstrap_memmap.length -= npages * page_size;

            return reinterpret_cast<void *>(lib::tohh(ret));
        }

        void init_pfndb()
        {
            mem.pfndb_base = lib::tohh(lib::align_up(mem.top, lib::gib(1)));
            log::debug("pmm: pfndb base: 0x{:X}", mem.pfndb_base);

            const std::size_t num_pages = lib::div_roundup(mem.usable_top, page_size);
            mem.pfndb_end = mem.pfndb_base + num_pages * sizeof(page);

            const auto memmaps = boot::requests::memmap.response->entries;
            const std::size_t num = boot::requests::memmap.response->entry_count;

            const auto start = mem.used;

            const auto add_memmap = [&memmaps](auto memmap, bool check = false)
            {
                const auto pg = reinterpret_cast<std::uintptr_t>(page_for(memmap->base));
                const auto vstart = lib::align_down(pg, page_size);
                const auto length = lib::align_up(memmap->length / page_size * sizeof(page), page_size);

                for (std::size_t vaddr = vstart; vaddr <= vstart + length; vaddr += page_size)
                {
                    if (check && memmaps[bootstrap_memmap_idx]->length < page_size * 5 /* 5? */) [[unlikely]]
                    {
                        memmaps[bootstrap_memmap_idx]->length = 0;
                        break;
                    }

                    const auto psize = vmm::page_size::small;
                    const auto flags = vmm::pflag::rw;

                    const auto paddr = alloc<std::uintptr_t>(1, true);
                    if (const auto ret = vmm::kernel_pagemap->map(vaddr, paddr, page_size, flags, psize); !ret)
                        lib::panic("could not map pfndb: {}", magic_enum::enum_name(ret.error()));
                }
            };

            for (std::size_t i = 0; i < num; i++)
            {
                const auto memmap = memmaps[i];
                const auto type = static_cast<boot::memmap>(memmap->type);
                if (type != boot::memmap::usable && type != boot::memmap::bootloader)
                    continue;

                if (i == bootstrap_memmap_idx)
                    continue;

                add_memmap(memmap);
            }
            add_memmap(memmaps[bootstrap_memmap_idx], true);

            log::debug("pmm: pfndb using {} kib", (mem.used - start) / 1024);
        }
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

    void *alloc(std::size_t npages, bool clear, bool low_mem)
    {
        if (npages == 0)
            return nullptr;

        const std::unique_lock _ { lock };
        const auto size = npages * page_size;

        std::pair<void *, std::size_t> ret { nullptr, 0 };
        if (initialised)
        {
#if defined(__x86_64__)
            if (low_mem)
                ret = lowmem.alloc(npages);
            else
#endif
                ret = general.alloc(npages);
#if defined(__x86_64__)
            if (!ret.first && !low_mem)
                ret = lowmem.alloc(npages);
#else
            lib::unused(low_mem);
#endif
        }
        else ret = { bootstrap_alloc(npages), size };

        if (!ret.first)
            lib::panic("pmm: out of memory");

        if (clear)
            std::memset(ret.first, 0, size);

        mem.used += ret.second;
        return lib::fromhh(ret.first);
    }

    void free(void *ptr, std::size_t npages)
    {
        if (npages == 0)
            return;

        if (initialised)
        {
            const std::unique_lock _ { lock };
#if defined(__x86_64__)
            if (is_lowmem(ptr))
            {
                mem.used -= lowmem.free(ptr, npages);
                return;
            }
#endif
            mem.used -= general.free(ptr, npages);
        }
        else lib::panic("attempted to free bootstrap memory");
    }

    initgraph::task reclaim_task
    {
        "pmm-reclaim-memory",
        initgraph::require { initramfs::extracted_stage() },
        [] {
            log::debug("pmm: reclaiming bootloader memory");

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
    };

    void init()
    {
        const auto memmaps = boot::requests::memmap.response->entries;
        const std::size_t num = boot::requests::memmap.response->entry_count;

        log::debug("pmm: number of memory maps: {}", num);

        for (std::size_t i = 0; i < num; i++)
        {
            const auto memmap = memmaps[i];
            const std::uintptr_t end = memmap->base + memmap->length;
            mem.top = std::max(mem.top, end);

            if (static_cast<boot::memmap>(memmap->type) == boot::memmap::usable)
                mem.usable_top = std::max(mem.usable_top, end);
        }

        log::info("pmm: setting up pfndb");
        init_pfndb();

        log::info("pmm: initialising the physical memory allocator");

        if (bootstrap_memmap_idx != static_cast<std::size_t>(-1)) [[likely]]
        {
            auto memmap = memmaps[bootstrap_memmap_idx];
            mem.usable += memmap->length - bootstrap_memmap.length;
            *memmap = bootstrap_memmap;
        }

        for (std::size_t i = 0; i < num; i++)
        {
            auto memmap = memmaps[i];

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

            // usable and bootloader reclaimable entries are page aligned
            if (memmap->base == 0)
            {
                if (memmap->length >= page_size * 2)
                {
                    memmap->base += page_size;
                    memmap->length -= page_size;
                }
                else
                {
                    mem.used += memmap->length;
                    mem.usable += memmap->length;
                    continue;
                }
            }

#if defined(__x86_64__)
            auto rstart = std::max(memmap->base, reserved_range_start);
            const std::uintptr_t end = memmap->base + memmap->length;
            const auto rend = std::min(end, reserved_range_end);

            if (reserved_range_start < end && memmap->base < reserved_range_end)
            {
                if (memmap->base < reserved_range_start)
                    add_range(memmap->base, reserved_range_start - memmap->base, false);
                if (end > reserved_range_end)
                    add_range(reserved_range_end, end - reserved_range_end, false);

                if (trampoline_pages == 0 && (rend - rstart) >= requested_size)
                {
                    trampoline_pages = rstart;
                    rstart += requested_size;
                }

                mem.used += rend - rstart;
                mem.usable -= rend - rstart;
                continue;
            }
#endif
            add_range(memmap->base, memmap->length, false);
        }

        // uacpi points or something idk
        for (std::size_t i = 0; i < 50; i++)
            general.split_to(0);

        initialised = true;
    }
} // namespace pmm