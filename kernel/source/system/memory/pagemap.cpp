// Copyright (C) 2024  ilobilo

module system.memory.virt;
import system.memory.phys;
import magic_enum;
import boot;
import lib;
import std;

import :pagemap;

namespace vmm
{
    namespace
    {
        std::uintptr_t vspace_base;
        std::uintptr_t vspaces[magic_enum::enum_count<vspace>()];
    } // namespace

    auto pagemap::getlvl(entry &entry, bool allocate) -> table *
    {
        table *ret = nullptr;
        if (!entry.getflags(valid_table_flags))
        {
            if (allocate == false)
                return nullptr;

            entry.clear();
            entry.setaddr(reinterpret_cast<std::uintptr_t>(ret = new_table()));
            entry.setflags(new_table_flags, true);
        }
        else ret = reinterpret_cast<table *>(entry.getaddr());

        return lib::tohh(ret);
    }

    std::expected<void, error> pagemap::map(std::uintptr_t vaddr, std::uintptr_t paddr, std::size_t length, flag flags, page_size psize, caching cache)
    {
        psize = fixpsize(psize);

        auto npsize = from_page_size(psize);
        if (paddr % npsize || vaddr % npsize)
            return std::unexpected { error::addr_not_aligned };

        std::unique_lock _ { _lock };

        auto aflags = to_arch(flags, cache, psize);

        for (std::size_t i = 0; i < length; i += npsize)
        {
            auto ret = getpte(vaddr + i, psize, true);
            if (!ret.has_value())
            {
                // umap
                for (std::size_t ii = 0; ii < i; ii += npsize)
                {
                    auto ret1 = getpte(vaddr + ii, psize, true);
                    if (!ret1.has_value())
                        return std::unexpected { ret1.error() };

                    auto &pte = ret1.value().get();
                    pte.clear();
                    invalidate(vaddr + ii);
                }
                return std::unexpected { ret.error() };
            }
            else
            {
                auto &pte = ret.value().get();
                pte.clear();
                pte.setaddr(paddr + i);
                pte.setflags(aflags, true);
            }
        }

        return { };
    }

    std::expected<void, error> pagemap::unmap(std::uintptr_t vaddr, std::size_t length, page_size psize)
    {
        lib::ensure(magic_enum::enum_contains(psize));

        if (vaddr % from_page_size(psize))
            return std::unexpected { error::addr_not_aligned };

        std::unique_lock _ { _lock };

        psize = fixpsize(psize);

        auto fact = from_page_size(psize);
        for (std::size_t i = 0; i < length; i += fact)
        {
            auto ret = getpte(vaddr + i, psize, false);
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            auto &pte = ret.value().get();
            pte.clear();
            invalidate(vaddr + i);
        }

        return { };
    }

    std::expected<std::uintptr_t, error> pagemap::translate(std::uintptr_t vaddr, page_size psize)
    {
        lib::ensure(magic_enum::enum_contains(psize));

        if (vaddr % from_page_size(psize))
            return std::unexpected { error::addr_not_aligned };

        std::unique_lock _ { _lock };

        psize = fixpsize(psize);

        auto ret = getpte(vaddr, psize, false);
        if (!ret.has_value())
            return std::unexpected { ret.error() };

        return ret.value().get().getaddr();
    }

    void init()
    {
        log::info("Initialising kernel pagemap");
        log::debug("HHDM offset is: 0x{:X}", boot::get_hhdm_offset());

        kernel_pagemap.initialise();

        log::debug("Mapping:");
        // {
        //     log::debug(" - First four gibibytes");

        //     auto psize = pagemap::max_page_size(lib::gib(4));
        //     if (!kernel_pagemap->map(boot::get_hhdm_offset(), 0, lib::gib(4), flag::rw, psize, caching::normal))
        //         lib::panic("Could not map virtual memory");
        // }
        {
            log::debug(" - Memory map entries");

            auto memmaps = boot::requests::memmap.response->entries;
            std::size_t num = boot::requests::memmap.response->entry_count;

            for (std::size_t i = 0; i < num; i++)
            {
                const auto memmap = memmaps[i];
                const auto type = static_cast<boot::memmap>(memmap->type);

                if (type != boot::memmap::usable && type != boot::memmap::bootloader_reclaimable &&
                    type != boot::memmap::kernel_and_modules && type != boot::memmap::framebuffer)
                    continue;

                const auto psize = pagemap::max_page_size(memmap->length);
                const auto npsize = pagemap::from_page_size(psize);

                const auto base = lib::align_down(memmap->base, npsize);
                const auto end = lib::align_up(memmap->base + memmap->length, npsize);

                // if (end < lib::gib(4))
                //     continue;

                auto cache = caching::normal;
                if (type == boot::memmap::framebuffer)
                    cache = caching::framebuffer;

                auto vaddr = lib::tohh(base);
                auto len = end - base;

                log::debug("   - type: {}, size: {} bytes, 0x{:X} -> 0x{:X}", magic_enum::enum_name(type), len, vaddr, base);

                if (!kernel_pagemap->map(vaddr, base, len, flag::rw, psize, cache))
                    lib::panic("Could not map virtual memory");
            }
        }
        {
            static constexpr auto flags = flag::rwx;
            static constexpr auto psize = page_size::small;
            static constexpr auto cache = caching::normal;

            const auto kernel_file = boot::requests::kernel_file.response->kernel_file;
            const auto kernel_addr = boot::requests::kernel_address.response;

            log::debug(" - Kernel binary: size: {} bytes, 0x{:X} -> 0x{:X}", kernel_file->size, kernel_addr->virtual_base, kernel_addr->physical_base);

            if (!kernel_pagemap->map(kernel_addr->virtual_base, kernel_addr->physical_base, kernel_file->size, flags, psize, cache))
                lib::panic("Could not map virtual memory");
        }

        log::debug("Loading the pagemap");
        kernel_pagemap->load();

        vspace_base = lib::tohh(lib::align_up(pmm::info().top, lib::gib(1)));
        for (std::size_t i = 0; auto &entry : vspaces)
            entry = vspace_base + (lib::gib(1) * (i++));
    }

    std::uintptr_t alloc_vpages(vspace type, std::size_t pages)
    {
        auto index = std::to_underlying(type);
        auto entry = &vspaces[index];

        auto increment = pages * pmm::page_size;

        if (type != vspace::other && increment && *entry + increment > (vspace_base + (lib::gib(1) * (index + 1))))
            entry = &vspaces[std::to_underlying(vspace::other)];

        auto ret = *entry;
        *entry += increment;
        return ret;
    }
} // namespace vmm