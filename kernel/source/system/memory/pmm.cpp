// Copyright (C) 2024-2025  ilobilo

module system.memory.phys;

import drivers.initramfs;
import system.scheduler;
import frigg;
import boot;
import lib;
import cppstd;

namespace pmm
{
    namespace
    {
        constinit lib::spinlock lock;
        constinit memory mem;

        constexpr std::size_t max_order = 15;
        struct page { page *next = nullptr; };
        constinit page lists[max_order + 1] { };

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

        inline void put(std::size_t order, page *pg)
        {
            pg->next = lists[order].next;
            lists[order].next = pg;
        }

        inline void *rem(std::size_t order)
        {
            const auto ret = lists[order].next;
            lists[order].next = lists[order].next->next;
            return ret;
        }

        inline bool has_pages(std::size_t order)
        {
            return lists[order].next != nullptr;
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
                const auto pg = lists[current].next;
                const auto pgaddr = reinterpret_cast<std::uintptr_t>(pg);

                lists[current].next = lists[current].next->next;
                current--;

                const auto buddy = reinterpret_cast<page *>(pgaddr + 0x1000 * lib::pow2(current));

                put(current, pg);
                put(current, buddy);
            }
        }

        void coalesce_to(std::size_t target)
        {
            // TODO:
            lib::unused(target);
        }

        void free(void *ptr, std::size_t npages, bool min)
        {
            if (npages == 0)
                return;

            const std::unique_lock _ { lock };

            const auto size = npages * page_size;
            const auto order = (min ? prev_order_from : next_order_from)(size);
            if (order < 0 || static_cast<std::size_t>(order) > max_order)
                return;

            put(order, static_cast<page *>(lib::tohh(ptr)));
            mem.used -= size;
        }
    } // namespace

    memory info()
    {
        const std::unique_lock _ { lock };
        return mem;
    }

    void *alloc(std::size_t npages, bool clear)
    {
        if (npages == 0)
            return nullptr;

        const std::unique_lock _ { lock };

        const auto size = npages * page_size;
        const auto order = next_order_from(size);
        if (order < 0 || static_cast<std::size_t>(order) > max_order)
            return nullptr;

        if (!has_pages(order))
        {
            if (order == max_order)
            {
                coalesce_to(max_order);
                if (!has_pages(order))
                    return nullptr;
                goto found;
            }
            split_to(order);
            if (!has_pages(order))
            {
                if (order != 0)
                {
                    coalesce_to(order);
                    if (!has_pages(order))
                        return nullptr;
                    goto found;
                }
                return nullptr;
            }
        }
        found:

        const auto ret = rem(order);
        if (clear)
            std::memset(ret, 0, size);

        mem.used += size;
        return lib::fromhh(ret);
    }

    void free(void *ptr, std::size_t npages)
    {
        free(ptr, npages, false);
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

                free(reinterpret_cast<void *>(memmap->base), memmap->length / page_size, true);
            }
        }
    };

    void init()
    {
        log::info("pmm: initialising the physical memory allocator");

        const auto memmaps = boot::requests::memmap.response->entries;
        const std::size_t num = boot::requests::memmap.response->entry_count;

        log::debug("pmm: number of memory maps: {}", num);

        for (std::size_t i = 0; i < num; i++)
        {
            auto memmap = memmaps[i];
            mem.top = std::max(mem.top, memmap->base + memmap->length);

            auto add_used = [&memmap]
            {
                mem.used += memmap->length;
                mem.usable += memmap->length;
            };

            const auto type = static_cast<boot::memmap>(memmap->type);
            if (type != boot::memmap::usable)
            {
                if (type == boot::memmap::kernel_and_modules || type == boot::memmap::bootloader)
                    add_used();
                continue;
            }

            if (memmap->base == 0)
            {
                if (memmap->length >= page_size * 2)
                {
                    memmap->base += page_size;
                    memmap->length -= page_size;
                }
                else
                {
                    add_used();
                    continue;
                }
            }

            const std::uintptr_t end = memmap->base + memmap->length;
            mem.usable_top = std::max(mem.usable_top, end);

            auto base = lib::tohh(memmap->base);
            auto size = memmap->length;
            mem.usable += size;

            while (size >= page_size)
            {
                const auto pg = reinterpret_cast<page *>(base);
                auto sorder = prev_order_from(size);
                if (sorder < 0)
                    break;

                auto order = std::min(static_cast<std::size_t>(sorder), max_order);
                while (base % (page_size * lib::pow2(order)))
                {
                    order--;
                    if (order < 0)
                        break;
                }

                put(order, pg);

                const auto offset = page_size * lib::pow2(order);
                size -= offset;
                base += offset;
            }

            // wasted
            mem.used += size;
        }

        // uacpi points or something idk
        for (std::size_t i = 0; i < 50; i++)
            split_to(0);
    }
} // namespace pmm