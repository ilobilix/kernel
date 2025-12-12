// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>

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
    } // namespace

    auto pagemap::getlvl(entry &entry, bool allocate) -> table *
    {
        table *ret = nullptr;

        auto accessor = entry.access();
        if (const auto addr = accessor.getaddr(); accessor.getflags(valid_table_flags) && is_canonical(addr))
            ret = reinterpret_cast<table *>(addr);
        else
        {
            if (allocate == false)
                return nullptr;

            accessor.clear()
                .setaddr(reinterpret_cast<std::uintptr_t>(ret = new_table()))
                .setflags(new_table_flags, true)
                .write();
        }

        return lib::tohh(ret);
    }

    auto pagemap::getpte(std::uintptr_t vaddr, page_size psize, bool allocate) -> std::expected<std::reference_wrapper<entry>, error>
    {
        static constexpr std::uintptr_t bits = 0b111111111;
        static constexpr std::size_t levels = 4;
        static constexpr std::size_t shift_start = 12 + (levels - 1) * 9;

        auto pml = lib::tohh(get_arch_table(vaddr));

        const auto retidx = levels - static_cast<std::size_t>(psize) - 1;
        auto shift = shift_start;

        for (std::size_t i = 0; i < levels; i++)
        {
            auto &entry = pml->entries[(vaddr >> shift) & bits];

            if (i == retidx)
                return std::ref(entry);

            pml = getlvl(entry, allocate);
            if (pml == nullptr)
                return std::unexpected { error::invalid_entry };

            shift -= 9;
        }
        std::unreachable();
    }

    std::expected<void, error> pagemap::map(std::uintptr_t vaddr, std::uintptr_t paddr, std::size_t length, pflag flags, page_size psize, caching cache)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));
        lib::bug_on(!magic_enum::enum_contains(cache));

        psize = fixpsize(psize);

        const auto npsize = from_page_size(psize);
        if (/* paddr % npsize || */ vaddr % npsize)
            return std::unexpected { error::addr_not_aligned };

        const std::unique_lock _ { _lock };

        const auto aflags = to_arch(flags, cache, psize);

        for (std::size_t i = 0; i < length; i += npsize)
        {
            const auto ret = getpte(vaddr + i, psize, true);
            if (!ret.has_value())
            {
                // unmap
                for (std::size_t ii = 0; ii < i; ii += npsize)
                {
                    const auto ret1 = getpte(vaddr + ii, psize, true);
                    if (!ret1.has_value())
                        return std::unexpected { ret1.error() };

                    auto &pte = ret1->get();
                    pte.access().clear().write();
                    invalidate(vaddr + ii);
                }
                return std::unexpected { ret.error() };
            }
            else
            {
                auto &pte = ret->get();
                pte.access()
                    .clear()
                    .setaddr(paddr + i)
                    .setflags(aflags, true)
                    .write();
            }
        }

        return { };
    }

    std::expected<void, error> pagemap::map_alloc(std::uintptr_t vaddr, std::size_t length, pflag flags, page_size psize, caching cache)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));
        lib::bug_on(!magic_enum::enum_contains(cache));

        struct page { page *next = nullptr; };
        page *current = nullptr;
        page head { };

        psize = fixpsize(psize);

        const auto npsize = from_page_size(psize);
        const auto npages = lib::div_roundup(npsize, pmm::page_size);

        for (std::size_t i = 0; i < length; i += npsize)
        {
            const auto paddr = pmm::alloc(npages, false);
            const auto pg = reinterpret_cast<page *>(lib::tohh(paddr));

            if (!head.next)
            {
                current = pg;
                head.next = current;
            }
            else
            {
                current->next = pg;
                current = current->next;
            }

            if (const auto ret = map(vaddr + i, paddr, npsize, flags, psize, cache); !ret)
            {
                current = head.next;
                while (current)
                {
                    const auto next = current->next;
                    pmm::free(reinterpret_cast<std::uintptr_t>(current), npages);
                    current = next;
                }
                return std::unexpected { ret.error() };
            }
        }

        current = head.next;
        while (current)
        {
            const auto next = current->next;
            std::memset(current, 0, npages * pmm::page_size);
            current = next;
        }
        return { };
    }

    std::expected<void, error> pagemap::protect(std::uintptr_t vaddr, std::size_t length, pflag flags, page_size psize, caching cache)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));
        lib::bug_on(!magic_enum::enum_contains(cache));

        psize = fixpsize(psize);
        const auto npsize = from_page_size(psize);
        if (vaddr % npsize)
            return std::unexpected { error::addr_not_aligned };

        const std::unique_lock _ { _lock };

        const auto aflags = to_arch(flags, cache, psize);

        for (std::size_t i = 0; i < length; i += npsize)
        {
            const auto ret = getpte(vaddr + i, psize, false);
            if (ret.has_value())
            {
                auto &pte = ret->get();
                pte.access()
                    .clearflags()
                    .setflags(aflags, true)
                    .write();
                invalidate(vaddr + i);
            }
            else return std::unexpected { ret.error() };
        }

        return { };
    }

    std::expected<void, error> pagemap::unmap(std::uintptr_t vaddr, std::size_t length, page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));

        const std::unique_lock _ { _lock };

        psize = fixpsize(psize);
        const auto npsize = from_page_size(psize);
        if (vaddr % npsize)
            return std::unexpected { error::addr_not_aligned };

        for (std::size_t i = 0; i < length; i += npsize)
        {
            const auto ret = getpte(vaddr + i, psize, false);

            // if there's a hole that's not mapped, don't throw an error
            if (!ret.has_value())
                continue;
                // return std::unexpected { ret.error() };

            auto &pte = ret->get();
            pte.access().clear().write();
            invalidate(vaddr + i);
        }

        return { };
    }

    std::expected<void, error> pagemap::unmap_dealloc(std::uintptr_t vaddr, std::size_t length, page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));

        psize = fixpsize(psize);
        const auto npsize = from_page_size(psize);

        for (std::size_t i = 0; i < length; i += npsize)
        {
            const auto rett = translate(vaddr + i, psize);
            if (!rett)
                return std::unexpected { rett.error() };

            const auto retu = unmap(vaddr + i, npsize, psize);
            if (!retu)
                return std::unexpected { retu.error() };

            pmm::free(rett.value());
        }

        return { };
    }

    std::expected<std::uintptr_t, error> pagemap::translate(std::uintptr_t vaddr, page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));

        const std::unique_lock _ { _lock };

        psize = fixpsize(psize);
        if (vaddr % from_page_size(psize))
            return std::unexpected { error::addr_not_aligned };

        const auto ret = getpte(vaddr, psize, false);
        if (!ret.has_value())
            return std::unexpected { ret.error() };

        const auto addr = ret->get().access().getaddr();
        if (!is_canonical(addr))
            return std::unexpected { error::invalid_entry };

        return addr;
    }

    pagemap::~pagemap()
    {
        lib::warn("vmm: destroying a pagemap");

        [](this auto self, table *ptr, std::size_t start, std::size_t end, std::size_t level)
        {
            if (level == 0)
                return;

            for (std::size_t i = start; i < end; i++)
            {
                auto lvl = getlvl(ptr->entries[i], false);
                if (lvl == nullptr)
                    continue;

                self(lvl, 0, 512, level - 1);
            }
            free_table(lib::fromhh(ptr));
        } (lib::tohh(_table), 0, 256, 4);
    }

    void init()
    {
        lib::info("vmm: setting up the kernel pagemap");
        lib::debug("vmm: hhdm offset: 0x{:X}", boot::get_hhdm_offset());

        kernel_pagemap.initialize();

        lib::debug("vmm: mapping:");
        {
            lib::debug("vmm: - memory map entries");

            const auto memmaps = boot::requests::memmap.response->entries;
            const std::size_t num = boot::requests::memmap.response->entry_count;

            for (std::size_t i = 0; i < num; i++)
            {
                const auto memmap = memmaps[i];
                const auto type = static_cast<boot::memmap>(memmap->type);

                if (type != boot::memmap::usable && type != boot::memmap::bootloader &&
                    type != boot::memmap::kernel_and_modules && type != boot::memmap::framebuffer)
                    continue;

                if (memmap->length == 0)
                    continue;

                const auto psize = pagemap::max_page_size(memmap->base, memmap->length);
                const auto npsize = pagemap::from_page_size(psize);

                const auto base = lib::align_down(memmap->base, npsize);
                const auto end = lib::align_up(memmap->base + memmap->length, npsize);

                auto cache = caching::normal;
                if (type == boot::memmap::framebuffer)
                    cache = caching::framebuffer;

                const auto vaddr = lib::tohh(base);
                const auto len = end - base;

                if (len == 0)
                    continue;

                lib::debug("vmm: -  type: {}, size: 0x{:X} bytes, 0x{:X} -> 0x{:X}", magic_enum::enum_name(type), len, memmap->base, vaddr);

                if (const auto ret = kernel_pagemap->map(vaddr, base, len, pflag::rw, psize, cache); !ret)
                    lib::panic("could not map virtual memory: {}", magic_enum::enum_name(ret.error()));
            }
        }
        {
            static constexpr auto psize = page_size::small;
            static constexpr auto cache = caching::normal;

            const auto kernel_file = boot::requests::kernel_file.response->executable_file;
            const auto kernel_addr = boot::requests::kernel_address.response;

            const auto ehdr = reinterpret_cast<Elf64_Ehdr *>(kernel_file->address);
            auto phdr = reinterpret_cast<Elf64_Phdr *>(reinterpret_cast<std::byte *>(kernel_file->address) + ehdr->e_phoff);

            lib::debug("vmm: - kernel binary");

            for (std::size_t i = 0; i < ehdr->e_phnum; i++)
            {
                if (phdr->p_type == PT_LOAD)
                {
                    const std::uintptr_t paddr = phdr->p_vaddr - kernel_addr->virtual_base + kernel_addr->physical_base;
                    const std::uintptr_t vaddr = phdr->p_vaddr;
                    const auto size = phdr->p_memsz;

                    auto flags = pflag::global;
                    if (phdr->p_flags & PF_R)
                        flags |= pflag::read;
                    if (phdr->p_flags & PF_W)
                        flags |= pflag::write;
                    if (phdr->p_flags & PF_X)
                        flags |= pflag::exec;

                    lib::debug("vmm: -  phdr: size: 0x{:X} bytes, flags: 0b{:b}, 0x{:X} -> 0x{:X}", size, static_cast<std::uint8_t>(flags), paddr, vaddr);

                    if (const auto ret = kernel_pagemap->map(vaddr, paddr, size, flags, psize, cache); !ret)
                        lib::panic("could not map virtual memory: {}", magic_enum::enum_name(ret.error()));
                }
                phdr = reinterpret_cast<Elf64_Phdr *>(reinterpret_cast<std::byte *>(phdr) + ehdr->e_phentsize);
            }
        }

        lib::debug("vmm: loading the pagemap");
        kernel_pagemap->load();
    }

    void init_vspaces()
    {
        vspace_base = lib::tohh(lib::align_up(pmm::info().free_start(), lib::gib(1)));
    }

    std::uintptr_t alloc_vspace(std::size_t pages)
    {
        const auto ret = vspace_base;
        vspace_base += pages * pmm::page_size;
        return ret;
    }
} // namespace vmm