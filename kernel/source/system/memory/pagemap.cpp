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

    auto pagemap::getlvl(entry &entry, bool allocate, bool split, page_size psize) -> table *
    {
        table *ret = nullptr;

        auto accessor = entry.access();
        if (const auto addr = accessor.getaddr(); accessor.getflags(valid_table_flags) && is_canonical(addr))
        {
            if (accessor.is_large())
            {
                if (!allocate && !split)
                    return nullptr;

                lib::bug_on(psize == page_size::small);

                const auto smaller_psize = static_cast<page_size>(static_cast<std::size_t>(psize) - 1);
                const auto npsize = from_page_size(smaller_psize);

                const auto raw_flags = accessor.getflags();
                const auto [pflags, cache] = from_arch(raw_flags, psize);
                const auto flags = to_arch(pflags, cache, smaller_psize);

                accessor.clear()
                    .setaddr(reinterpret_cast<std::uintptr_t>(ret = new_table()))
                    .setflags(valid_table_flags, true)
                    .write();

                for (std::size_t i = 0; i < 512; i++)
                {
                    lib::tohh(ret)->entries[i].access()
                        .clear()
                        .setaddr(addr + i * npsize)
                        .setflags(flags, true)
                        .write();
                }
            }
            else ret = reinterpret_cast<table *>(addr);
        }
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

    auto pagemap::getpte(std::uintptr_t vaddr, page_size psize, bool allocate, bool split) -> std::expected<std::reference_wrapper<entry>, error>
    {
        static constexpr std::uintptr_t bits = 0b111111111;
        static constexpr std::size_t shift_start = 12 + (levels - 1) * 9;

        auto pml = lib::tohh(get_arch_table(vaddr));

        const auto retidx = levels - static_cast<std::size_t>(psize) - 1;
        auto shift = shift_start;

        for (std::size_t i = 0; i < levels; i++)
        {
            auto &entry = pml->entries[(vaddr >> shift) & bits];

            if (i == retidx)
                return std::ref(entry);

            const auto current_psize = static_cast<page_size>(levels - i - 1);
            pml = getlvl(entry, allocate, split, current_psize);
            if (pml == nullptr)
                return std::unexpected { error::invalid_entry };

            shift -= 9;
        }
        std::unreachable();
    }

    std::expected<void, error> pagemap::map(std::uintptr_t vaddr, std::uintptr_t paddr, std::size_t length, pflag flags, std::optional<page_size> psize, caching cache)
    {
        lib::bug_on(!magic_enum::enum_contains(cache));

        if (psize.has_value())
        {
            lib::bug_on(!magic_enum::enum_contains(psize.value()));

            psize = fixpsize(psize.value());

            const auto npsize = from_page_size(psize.value());
            if (paddr % npsize || vaddr % npsize)
                return std::unexpected { error::addr_not_aligned };
        }

        const std::unique_lock _ { _lock };

        std::uintptr_t current_vaddr = vaddr;
        std::uintptr_t current_paddr = paddr;
        std::size_t remaining = lib::align_up(length, pmm::page_size);

        while (remaining > 0)
        {
            const auto max_psize = fixpsize(max_page_size(current_vaddr, remaining));
            const auto use_psize = psize.has_value() ? psize.value() : max_psize;
            const auto npsize = from_page_size(use_psize);

            if (use_psize > max_psize)
                return std::unexpected { error::addr_not_aligned };

            const auto aflags = to_arch(flags, cache, use_psize);

            const auto ret = getpte(current_vaddr, use_psize, true, true);
            if (!ret.has_value())
            {
                lib::unused(unmap_internal(vaddr, current_vaddr - vaddr, std::nullopt));
                return std::unexpected { ret.error() };
            }

            auto &pte = ret->get();
            auto accessor = pte.access();

            const auto addr = accessor.getaddr();
            const bool needs_invl = addr && is_canonical(addr);

            if (accessor.getflags(valid_table_flags) && needs_invl)
                return std::unexpected { error::addr_in_use };

            accessor.clear()
                .setaddr(current_paddr)
                .setflags(aflags, true)
                .write();

            if (needs_invl)
                invalidate(current_vaddr);

            current_vaddr += npsize;
            current_paddr += npsize;
            remaining -= npsize;
        }

        return { };
    }

    std::expected<void, error> pagemap::protect(std::uintptr_t vaddr, std::size_t length, pflag flags, std::optional<page_size> psize, caching cache)
    {
        lib::bug_on(!magic_enum::enum_contains(cache));

        if (psize.has_value())
        {
            lib::bug_on(!magic_enum::enum_contains(psize.value()));

            psize = fixpsize(psize.value());
            if (vaddr % from_page_size(psize.value()))
                return std::unexpected { error::addr_not_aligned };
        }

        const std::unique_lock _ { _lock };

        std::uintptr_t current_vaddr = vaddr;
        std::size_t remaining = lib::align_up(length, pmm::page_size);

        while (remaining > 0)
        {
            const auto max_psize = fixpsize(max_page_size(current_vaddr, remaining));
            const auto use_psize = psize.has_value() ? psize.value() : max_psize;
            const auto npsize = from_page_size(use_psize);

            if (use_psize > max_psize)
                return std::unexpected { error::addr_not_aligned };

            const auto ret = getpte(current_vaddr, use_psize, false, true);
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            auto &pte = ret->get();
            auto accessor = pte.access();

            if (use_psize != page_size::small && !accessor.is_large())
                return std::unexpected { error::addr_in_use };

            accessor.clearflags()
                .setflags(to_arch(flags, cache, use_psize), true)
                .write();
            invalidate(current_vaddr);

            current_vaddr += npsize;
            remaining -= npsize;
        }
        return { };
    }

    std::expected<void, error> pagemap::unmap_internal(std::uintptr_t vaddr, std::size_t length, std::optional<page_size> psize)
    {
        std::uintptr_t current_vaddr = vaddr;
        std::size_t remaining = lib::align_up(length, pmm::page_size);

        while (remaining > 0)
        {
            const auto max_psize = fixpsize(max_page_size(current_vaddr, remaining));
            const auto use_psize = psize.has_value() ? psize.value() : max_psize;
            const auto npsize = from_page_size(use_psize);

            if (use_psize > max_psize)
                return std::unexpected { error::addr_not_aligned };

            const auto ret = getpte(current_vaddr, use_psize, false, true);
            if (!ret.has_value())
            {
                // return std::unexpected { ret.error() };
                current_vaddr += npsize;
                remaining -= npsize;
                continue;
            }

            auto &pte = ret->get();
            pte.access().clear().write();
            invalidate(current_vaddr);

            current_vaddr += npsize;
            remaining -= npsize;
        }
        return { };
    }

    std::expected<void, error> pagemap::unmap(std::uintptr_t vaddr, std::size_t length, std::optional<page_size> psize)
    {
        if (psize.has_value())
        {
            lib::bug_on(!magic_enum::enum_contains(psize.value()));

            psize = fixpsize(psize.value());
            if (vaddr % from_page_size(psize.value()))
                return std::unexpected { error::addr_not_aligned };
        }

        const std::unique_lock _ { _lock };
        return unmap_internal(vaddr, length, psize);
    }

    std::expected<std::uintptr_t, error> pagemap::translate(std::uintptr_t vaddr, page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));

        const std::unique_lock _ { _lock };

        psize = fixpsize(psize);
        if (vaddr % from_page_size(psize))
            return std::unexpected { error::addr_not_aligned };

        const auto ret = getpte(vaddr, psize, false, false);
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
                auto lvl = getlvl(ptr->entries[i], false, false, static_cast<page_size>(level - 1));
                if (lvl == nullptr)
                    continue;

                self(lvl, 0, 512, level - 1);
            }
            free_table(lib::fromhh(ptr));
        } (lib::tohh(_table), 0, 256, levels);
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

                auto cache = caching::normal;
                if (type == boot::memmap::framebuffer)
                    cache = caching::framebuffer;

                const auto paddr = memmap->base;
                const auto vaddr = lib::tohh(paddr);
                const auto len = memmap->length;

                if (len == 0)
                    continue;

                lib::debug("vmm: -  type: {}, size: 0x{:X} bytes, 0x{:X} -> 0x{:X}", magic_enum::enum_name(type), len, memmap->base, vaddr);

                if (const auto ret = kernel_pagemap->map(vaddr, paddr, len, pflag::rw, std::nullopt, cache); !ret)
                    lib::panic("could not map virtual memory: {}", magic_enum::enum_name(ret.error()));
            }
        }
        {
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

                    if (const auto ret = kernel_pagemap->map(vaddr, paddr, size, flags, std::nullopt, cache); !ret)
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