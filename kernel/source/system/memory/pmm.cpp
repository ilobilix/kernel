// Copyright (C) 2024-2026  ilobilo

module system.memory.phys;

import drivers.fs.procfs;
import drivers.initramfs;
import system.memory.virt;
import magic_enum;
import frigg;
import boot;
import lib;
import fmt;

namespace pmm
{
    namespace
    {
        constinit lib::spinlock_irq lock;
        constinit memory mem;
        constinit bool initialised = false;

        template<std::uintptr_t Start, std::uintptr_t End>
        struct allocator
        {
            static constexpr std::uintptr_t start = Start;
            static constexpr std::uintptr_t end = End;

            struct list { list *prev = nullptr; list *next = nullptr; };
            list lists[max_order + 1] { };

            static bool in_range(const auto ptr)
            {
                const auto addr = lib::fromhh(reinterpret_cast<std::uintptr_t>(ptr));
                return addr >= start && addr < end;
            }

            static bool in_range(std::uintptr_t rstart, std::uintptr_t rend)
            {
                return lib::range_overlaps(rstart, rend, start, end);
            }

            static auto range_intersection(std::uintptr_t rstart, std::uintptr_t rend)
            {
                return lib::range_intersection(rstart, rend, start, end);
            }

            static std::ptrdiff_t prev_order_from(std::size_t size)
            {
                if (size < page_size)
                    return -1;
                return lib::log2(std::bit_floor(size)) - 12;
            }

            static std::ptrdiff_t next_order_from(std::size_t size)
            {
                if (size < page_size)
                    return -1;
                return lib::log2(std::bit_ceil(size)) - 12;
            }

            static std::uintptr_t get_phys(const list *pg)
            {
                return lib::fromhh(reinterpret_cast<std::uintptr_t>(pg));
            }

            static std::uintptr_t get_buddy(std::uintptr_t paddr, std::size_t order)
            {
                return paddr ^ (page_size << order);
            }

            static bool buddy_valid(std::uintptr_t paddr, std::size_t order)
            {
                const auto size = page_size << order;
                const auto buddy = get_buddy(paddr, order);

                if (buddy < start || buddy + size > end)
                    return false;

                return buddy + size <= mem.usable_top;
            }

            void put(std::size_t order, list *pg)
            {
                vmm::page_for(pg)->buddy.listed = 1;

                pg->next = lists[order].next;
                pg->prev = nullptr;
                if (lists[order].next)
                    lists[order].next->prev = pg;
                lists[order].next = pg;
            }

            void *rem(std::size_t order)
            {
                const auto ret = lists[order].next;
                vmm::page_for(ret)->buddy.listed = 0;

                lists[order].next = ret->next;
                if (lists[order].next)
                    lists[order].next->prev = nullptr;
                return ret;
            }

            bool has_pages(std::size_t order)
            {
                return lists[order].next != nullptr;
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

                    vmm::page_for(base)->buddy.order = order;

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
                    const auto pg = static_cast<list *>(rem(current));
                    const auto pgaddr = reinterpret_cast<std::uintptr_t>(pg);

                    current--;

                    const auto buddy = reinterpret_cast<list *>(
                        pgaddr + (page_size * lib::pow2(current))
                    );

                    auto *pg_page = vmm::page_for(pgaddr);
                    auto *buddy_page = vmm::page_for(reinterpret_cast<std::uintptr_t>(buddy));

                    lib::bug_on(pg_page->buddy.allocated != 0);
                    lib::bug_on(pg_page->buddy.listed != 0);
                    lib::bug_on(buddy_page->buddy.allocated != 0);
                    lib::bug_on(buddy_page->buddy.listed != 0);

                    pg_page->buddy.order = current;
                    buddy_page->buddy.order = current;

                    put(current, pg);
                    put(current, buddy);
                }
            }

            void coalesce_to(std::size_t target)
            {
                if (target == 0)
                    return;

                target = std::min(target, max_order);

                for (std::size_t order = 0; order < target; order++)
                {
                    const auto next_block_size = page_size * lib::pow2(order + 1);
                    auto curr = lists[order].next;

                    while (has_pages(order))
                    {
                        while (get_phys(curr) % next_block_size ||
                            !buddy_valid(get_phys(curr), order))
                        {
                            curr = curr->next;
                            if (!curr)
                                goto end;
                        }

                        const auto curr_page = vmm::page_for(curr);
                        lib::bug_on(curr_page->buddy.allocated != 0);
                        lib::bug_on(curr_page->buddy.order != order);

                        const auto buddy_addr = lib::tohh(get_buddy(get_phys(curr), order));
                        const auto buddy_page = vmm::page_for(buddy_addr);

                        // if it's not allocated or listed, then it's not a fren
                        if (!buddy_page->buddy.listed || buddy_page->buddy.order != order)
                        {
                            // cannot coalesce :(
                            curr = curr->next;
                            if (!curr)
                                goto end;
                            continue;
                        }

                        lib::bug_on(buddy_page->buddy.allocated != 0);

                        auto remove = [this, &order](auto pg)
                        {
                            vmm::page_for(pg)->buddy.listed = 0;

                            if (pg->prev)
                                pg->prev->next = pg->next;
                            else
                                lists[order].next = pg->next;

                            if (pg->next)
                                pg->next->prev = pg->prev;
                        };

                        remove(curr);
                        remove(reinterpret_cast<list *>(buddy_addr));

                        auto *merged = reinterpret_cast<list *>(curr);
                        auto *merged_page = vmm::page_for(reinterpret_cast<std::uintptr_t>(merged));

                        merged_page->buddy.order = order + 1;
                        put(order + 1, merged);

                        curr = lists[order].next;
                    }
                    end:
                }
            }

            std::size_t free(std::uintptr_t addr, std::size_t npages)
            {
                const auto size = npages * page_size;
                const auto order = next_order_from(size);
                if (order < 0 || static_cast<std::size_t>(order) > max_order)
                    return 0;

                auto *pg = vmm::page_for(addr);
                lib::bug_on(
                    pg->buddy.allocated == 0,
                    "pmm::free: not allocated: addr=0x{:X} npages={} order={} pg->order={}",
                    addr, npages, order, static_cast<std::size_t>(pg->buddy.order)
                );
                lib::bug_on(
                    pg->buddy.order != order,
                    "pmm::free: order mismatch: addr=0x{:X} npages={} expected order={} pg->order={}",
                    addr, npages, order, static_cast<std::size_t>(pg->buddy.order)
                );
                pg->buddy.allocated = 0;

                put(order, reinterpret_cast<list *>(lib::tohh(addr)));
                return size;
            }

            std::pair<std::uintptr_t, std::size_t> alloc(std::size_t npages)
            {
                const auto size = npages * page_size;
                const auto order = next_order_from(size);
                const auto real_size = page_size * lib::pow2(order);

                if (order < 0 || static_cast<std::size_t>(order) > max_order)
                    return { 0, 0 };

                if (!has_pages(order))
                {
                    if (order == max_order)
                    {
                        coalesce_to(max_order);
                        if (!has_pages(order))
                            return { 0, 0 };
                        goto found;
                    }
                    split_to(order);
                    if (!has_pages(order))
                    {
                        if (order != 0)
                        {
                            coalesce_to(order);
                            if (!has_pages(order))
                                return { 0, 0 };
                            goto found;
                        }
                        return { 0, 0 };
                    }
                }
                found:
                const auto ret = reinterpret_cast<std::uintptr_t>(rem(order));

                auto *pg = vmm::page_for(ret);
                lib::bug_on(
                    pg->buddy.allocated == 1,
                    "pmm::alloc: already allocated: addr=0x{:X} order={} pg->order={}",
                    ret, order, static_cast<std::size_t>(pg->buddy.order)
                );
                lib::bug_on(
                    pg->buddy.order != order,
                    "pmm::alloc: order mismatch: addr=0x{:X} expected order={} pg->order={}",
                    ret, order, static_cast<std::size_t>(pg->buddy.order)
                );
                pg->buddy.allocated = 1;

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
        }

        std::size_t bootstrap_memmap_idx = -1;
        boot::limine_memmap_entry bootstrap_memmap;

        std::uintptr_t bootstrap_alloc(std::size_t npages)
        {
            lib::bug_on(npages != 1);

            // first called when allocating the pagemap
            [[maybe_unused]]
            static const auto once = [] {
                lib::info("pmm: setting up bootstrap allocator");

                const auto *memmaps = boot::requests::memmap.response->entries;
                const std::size_t num = boot::requests::memmap.response->entry_count;

                std::size_t max_size = 0, idx = -1;
                for (std::size_t i = 0; i < num; i++)
                {
                    const auto *memmap = memmaps[i];
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

            const auto ret = bootstrap_memmap.base + bootstrap_memmap.length - (npages * page_size);
#if defined(__x86_64__)
            if (ret < lib::mib(1))
                lib::panic("pmm: bootstrap allocator tried to allocate memory below 1 MiB");
#endif
            bootstrap_memmap.length -= npages * page_size;

            return lib::tohh(ret);
        }

        void pfndb_add(std::size_t idx)
        {
            const auto *memmaps = boot::requests::memmap.response->entries;
            const auto *memmap = memmaps[idx];

            const auto pg = reinterpret_cast<std::uintptr_t>(vmm::page_for(memmap->base));
            const auto vstart = lib::align_down(pg, page_size);

            const auto max_length = lib::align_up(memmap->length / page_size * sizeof(vmm::page), page_size) + page_size;

            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::rwg;

            for (std::size_t vaddr = vstart; vaddr < vstart + max_length; vaddr += page_size)
            {
                if (idx == bootstrap_memmap_idx && bootstrap_memmap.length < page_size * 5 /* 5? */)
                    lib::panic("pmm: could not map pfndb: out of memory");

                if (const auto ret = vmm::kernel_pagemap->translate(vaddr, psize); ret && ret.value() != 0)
                    continue;

                const auto paddr = alloc(1, true);
                if (const auto ret = vmm::kernel_pagemap->map(vaddr, paddr, page_size, flags, psize); !ret)
                    lib::panic("pmm: could not map pfndb: {}", lib::error_name(ret.error()));
            }
        };

        void pfndb_fill_holes()
        {
            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::read | vmm::pflag::global;

            const auto zero = alloc(1, true);
            std::size_t num = 0;

            const auto vstart = lib::align_down(mem.pfndb_base, page_size);
            const auto vend = lib::align_up(mem.pfndb_end, page_size);

            for (std::uintptr_t vaddr = vstart; vaddr < vend; vaddr += page_size)
            {
                if (const auto ret = vmm::kernel_pagemap->translate(vaddr, psize); ret && ret.value() != 0)
                    continue;

                if (const auto ret = vmm::kernel_pagemap->map(vaddr, zero, page_size, flags, psize); !ret)
                    lib::panic("pmm: could not map pfndb hole: {}", lib::error_name(ret.error()));

                num++;
            }

            lib::debug("pmm: filled {} hole pages for pfndb", num);
        }
    } // namespace

    memory info() { return mem; }

    [[nodiscard]]
    std::uintptr_t alloc(std::size_t count, bool clear, type tp)
    {
        if (count == 0)
            return 0;

        const std::unique_lock _ { lock };
        const auto size = count * page_size;

        std::pair<std::uintptr_t, std::size_t> ret { 0, 0 };
        if (initialised)
        {
            switch (tp)
            {
                case type::normal:
                    ret = normal.alloc(count);
                    if (!ret.first && bootstrap_memmap_idx != static_cast<std::size_t>(-1))
                        ret = { bootstrap_alloc(count), size };
                    if (!ret.first)
                        ret = sub4gib.alloc(count);
#if !defined(__x86_64__)
                    if (!ret.first)
                        ret = sub1mib.alloc(count);
#endif
                    break;
                case type::sub4gib:
                    ret = sub4gib.alloc(count);
                    break;
                case type::sub1mib:
                    ret = sub1mib.alloc(count);
                    break;
                default:
                    lib::panic("pmm: unknown allocation type {}", magic_enum::enum_name(tp));
            }
        }
        else ret = { bootstrap_alloc(count), size };

        if (!ret.first)
        {
            lib::panic(
                "pmm: could not allocate {} page{}. type: {}",
                count, count == 1 ? "" : "s", magic_enum::enum_name(tp)
            );
        }

        if (clear)
            std::memset(reinterpret_cast<void *>(ret.first), 0, size);

        mem.used += ret.second;
        return lib::fromhh(ret.first);
    }

    void free(std::uintptr_t addr, std::size_t count)
    {
        if (count == 0 || addr == 0)
            return;

        if (initialised)
        {
            const std::unique_lock _ { lock };

            if (sub1mib.in_range(addr))
                mem.used -= sub1mib.free(addr, count);
            else if (sub4gib.in_range(addr))
                mem.used -= sub4gib.free(addr, count);
            else if (normal.in_range(addr))
                mem.used -= normal.free(addr, count);
            else
                lib::panic("pmm: attempted to free memory outside managed ranges: 0x{:X}", addr);
        }
        else lib::panic("pmm: attempted to free bootstrap memory");
    }

    void reclaim_bootloader_memory()
    {
        lib::debug("pmm: reclaiming bootloader memory");

        const auto *memmaps = boot::requests::memmap.response->entries;
        const std::size_t num = boot::requests::memmap.response->entry_count;

        for (std::size_t i = 0; i < num; i++)
        {
            const auto *memmap = memmaps[i];
            if (static_cast<boot::memmap>(memmap->type) != boot::memmap::bootloader)
                continue;

            add_range(memmap->base, memmap->length, true);
        }
    }

    void init()
    {
        const auto *memmaps = boot::requests::memmap.response->entries;
        const std::size_t num = boot::requests::memmap.response->entry_count;

        lib::debug("pmm: number of memory maps: {}", num);

        for (std::size_t i = 0; i < num; i++)
        {
            const auto *memmap = memmaps[i];
            const auto type = static_cast<boot::memmap>(memmap->type);

            const std::uintptr_t end = memmap->base + memmap->length;
            mem.top = std::max(mem.top, end);

            if (type != boot::memmap::usable && type != boot::memmap::bootloader && type != boot::memmap::kernel_and_modules)
                continue;

            mem.usable_top = std::max(mem.usable_top, end);
        }

        std::size_t pfndb_used_total = 0;
        {
            lib::info("pmm: setting up pfndb");

            mem.pfndb_base = lib::tohh(lib::align_up(mem.top, lib::gib(1)));
            lib::debug("pmm: pfndb base: 0x{:X}", mem.pfndb_base);

            const std::size_t num_pages = lib::div_roundup(mem.usable_top, page_size);
            mem.pfndb_end = mem.pfndb_base + (num_pages * sizeof(vmm::page));

            const std::size_t start = mem.used;

            for (std::size_t i = 0; i < num; i++)
            {
                const auto *memmap = memmaps[i];
                const auto type = static_cast<boot::memmap>(memmap->type);

                if (type != boot::memmap::usable && type != boot::memmap::bootloader && type != boot::memmap::kernel_and_modules)
                    continue;

                pfndb_add(i);
            }

            pfndb_fill_holes();

            pfndb_used_total = mem.used - start;
            lib::debug("pmm: pfndb size: {} KiB", pfndb_used_total / lib::kib(1));
        }

        lib::info("pmm: initialising the physical memory allocator");

        for (std::size_t i = 0; i < num; i++)
        {
            if (i == bootstrap_memmap_idx)
                continue;

            const auto *memmap = memmaps[i];
            const auto type = static_cast<boot::memmap>(memmap->type);

            if (type != boot::memmap::usable)
            {
                if (type == boot::memmap::bootloader || type == boot::memmap::kernel_and_modules)
                {
                    mem.used += memmap->length;
                    mem.usable += memmap->length;
                }
                continue;
            }

            add_range(memmap->base, memmap->length, false);
        }

        initialised = true;

        lib::debug("pmm: adding bootstrap memory to allocator");
        *memmaps[bootstrap_memmap_idx] = bootstrap_memmap;
        add_range(bootstrap_memmap.base, bootstrap_memmap.length, false);

        bootstrap_memmap_idx = -1;
    }

    lib::initgraph::task procfs_register_task
    {
        "pmm.procfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::procfs::registered_stage() },
        [] {
            using namespace ::fs::procfs;
            lib::bug_on(!register_global("meminfo",
                make_file_ops([](auto) {
                    // TODO: Active/Inactive/Slab/AnonPages/Mapped/correct MemAvailable
                    const auto mem = info();
                    const auto kb = [](std::size_t bytes) { return bytes / 1024; };

                    const auto shmem_pages = vmm::cached_pages(vmm::object_type::shmem);
                    const auto file_pages = vmm::cached_pages(vmm::object_type::file);
                    const auto shmem_bytes = shmem_pages * pmm::page_size;
                    const auto cached_bytes = (shmem_pages + file_pages) * pmm::page_size;
                    const auto free_bytes = mem.usable - mem.used;

                    return fmt::format(
                        "MemTotal:       {} kB\n"
                        "MemFree:        {} kB\n"
                        "MemAvailable:   {} kB\n"
                        "Buffers:        {} kB\n"
                        "Cached:         {} kB\n"
                        "Shmem:          {} kB\n"
                        "SwapTotal:      {} kB\n"
                        "SwapFree:       {} kB\n",
                        kb(mem.usable),
                        kb(free_bytes),
                        kb(free_bytes + cached_bytes),
                        0uz,
                        kb(cached_bytes),
                        kb(shmem_bytes),
                        0uz,
                        0uz
                    );
                }), node_type::file, 0444
            ));
        }
    };
} // namespace pmm
