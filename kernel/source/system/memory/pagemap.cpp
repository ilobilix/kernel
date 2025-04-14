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
        std::uintptr_t vspaces[magic_enum::enum_count<space_type>()];
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

        const auto npsize = from_page_size(psize);
        if (paddr % npsize || vaddr % npsize)
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

        const std::unique_lock _ { _lock };

        psize = fixpsize(psize);

        const auto fact = from_page_size(psize);
        for (std::size_t i = 0; i < length; i += fact)
        {
            const auto ret = getpte(vaddr + i, psize, false);
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

        const std::unique_lock _ { _lock };

        psize = fixpsize(psize);

        const auto ret = getpte(vaddr, psize, false);
        if (!ret.has_value())
            return std::unexpected { ret.error() };

        return ret.value().get().getaddr();
    }

    void init()
    {
        log::info("vmm: setting up the kernel pagemap");
        log::debug("vmm: HHDM offset is: 0x{:X}", boot::get_hhdm_offset());

        kernel_pagemap.initialize();

        log::debug("vmm: mapping:");
        {
            log::debug("vmm: - memory map entries");

            const auto memmaps = boot::requests::memmap.response->entries;
            const std::size_t num = boot::requests::memmap.response->entry_count;

            for (std::size_t i = 0; i < num; i++)
            {
                const auto memmap = memmaps[i];
                const auto type = static_cast<boot::memmap>(memmap->type);

                if (type != boot::memmap::usable && type != boot::memmap::bootloader &&
                    type != boot::memmap::kernel_and_modules && type != boot::memmap::framebuffer)
                    continue;

                const auto psize = pagemap::max_page_size(memmap->length);
                const auto npsize = pagemap::from_page_size(psize);

                const auto base = lib::align_down(memmap->base, npsize);
                const auto end = lib::align_up(memmap->base + memmap->length, npsize);

                auto cache = caching::normal;
                if (type == boot::memmap::framebuffer)
                    cache = caching::framebuffer;

                const auto vaddr = lib::tohh(base);
                const auto len = end - base;

                log::debug("vmm: -  type: {}, size: 0x{:X} bytes, 0x{:X} -> 0x{:X}", magic_enum::enum_name(type), len, vaddr, base);

                if (!kernel_pagemap->map(vaddr, base, len, flag::rw, psize, cache))
                    lib::panic("could not map virtual memory");
            }
        }
        {
            static constexpr auto psize = page_size::small;
            static constexpr auto cache = caching::normal;

            const auto kernel_file = boot::requests::kernel_file.response->executable_file;
            const auto kernel_addr = boot::requests::kernel_address.response;

            const auto ehdr = reinterpret_cast<Elf64_Ehdr *>(kernel_file->address);
            auto phdr = reinterpret_cast<Elf64_Phdr *>(reinterpret_cast<std::byte *>(kernel_file->address) + ehdr->e_phoff);

            log::debug("vmm: - kernel binary");

            for (std::size_t i = 0; i < ehdr->e_phnum; i++)
            {
                if (phdr->p_type == PT_LOAD)
                {
                    const std::uintptr_t paddr = phdr->p_vaddr - kernel_addr->virtual_base + kernel_addr->physical_base;
                    const std::uintptr_t vaddr = phdr->p_vaddr;
                    const auto size = phdr->p_memsz;

                    auto flags = flag::none;
                    if (phdr->p_flags & PF_R)
                        flags |= flag::read;
                    if (phdr->p_flags & PF_W)
                        flags |= flag::write;
                    if (phdr->p_flags & PF_X)
                        flags |= flag::exec;

                    log::debug("vmm: -  phdr: size: 0x{:X} bytes, flags: 0b{:b}, 0x{:X} -> 0x{:X}", size, static_cast<std::uint8_t>(flags), paddr, vaddr);

                    if (!kernel_pagemap->map(vaddr, paddr, size, flags, psize, cache))
                        lib::panic("could not map virtual memory");
                }
                phdr = reinterpret_cast<Elf64_Phdr *>(reinterpret_cast<std::byte *>(phdr) + ehdr->e_phentsize);
            }
        }

        log::debug("vmm: loading the pagemap");
        kernel_pagemap->load();

        vspace_base = lib::tohh(lib::align_up(pmm::info().top, lib::gib(1)));
        for (std::size_t i = 0; auto &entry : vspaces)
            entry = vspace_base + (lib::gib(1 /* TODO: more than 1 gib? */) * (i++));
    }

    std::uintptr_t alloc_vpages(space_type type, std::size_t pages)
    {
        const auto index = std::to_underlying(type);
        auto entry = &vspaces[index];

        const auto increment = pages * pmm::page_size;

        if (type != space_type::other && increment && *entry + increment > (vspace_base + (lib::gib(1) * (index + 1))))
            entry = &vspaces[std::to_underlying(space_type::other)];

        const auto ret = *entry;
        *entry += increment;
        return ret;
    }
} // namespace vmm