// Copyright (C) 2024-2025  ilobilo

module system.memory.virt;

import system.memory.phys;
import system.cpu;
import magic_enum;
import lib;
import std;

import :pagemap;

namespace vmm
{
    namespace arch
    {
        namespace
        {
            enum flag : std::uintptr_t
            {
                present = (1 << 0),
                write = (1 << 1),
                user = (1 << 2),
                pwt = (1 << 3),
                pcd = (1 << 4),
                lpages = (1 << 7),
                pat = (1 << 7),
                global = (1 << 8),
                lpat = (1 << 12),
                no_exec = (1ul << 63)
            };

            constexpr std::size_t page_sizes[]
            {
                lib::kib(4), // page_size::small
                lib::mib(2), // page_size::medium
                lib::gib(1)  // page_size::large
            };

            // 1 gib pages supported
            bool large_pages = false;
        } // namespace
    } // namespace arch

    std::uintptr_t pagemap::pa_mask = 0x000FFFFFFFFFF000;

    const std::uintptr_t pagemap::valid_table_flags = arch::flag::present;
    const std::uintptr_t pagemap::new_table_flags = arch::flag::present | arch::flag::write | arch::flag::user;

    extern "C" constinit bool pagemap_use_lowmem = false;
    auto pagemap::new_table() -> table *
    {
        static_assert(sizeof(table) == pmm::page_size);
        const auto flag = pagemap_use_lowmem ? pmm::type::sub4gib : pmm::type::normal;
        return pmm::alloc<table *>(1, true, flag);
    }

    void pagemap::free_table(table *ptr)
    {
        pmm::free(ptr, 1);
    }

    page_size pagemap::fixpsize(page_size psize)
    {
        if (psize == page_size::large && !arch::large_pages)
            return page_size::medium;
        return psize;
    }

    void pagemap::invalidate(std::uintptr_t vaddr)
    {
        cpu::invlpg(vaddr);
    }

    std::uintptr_t pagemap::to_arch(pflag flags, caching cache, page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(cache));
        lib::bug_on(!magic_enum::enum_contains(psize));

        std::uintptr_t ret = 0;

        if ((flags & pflag::read) != pflag::none)
            ret |= arch::flag::present;
        if ((flags & pflag::write) != pflag::none)
            ret |= arch::flag::write;
        if ((flags & pflag::user) != pflag::none)
            ret |= arch::flag::user;
        if ((flags & pflag::global) != pflag::none)
            ret |= arch::flag::global;
        if ((flags & pflag::exec) == pflag::none)
            ret |= arch::flag::no_exec;

        if (psize != page_size::small)
            ret |= arch::flag::lpages;

        /*
         * write_back:         PAT0:  PAT = 0, PCD = 0, PWT = 0
         * write_through:      PAT1:  PAT = 0, PCD = 0, PWT = 1
         * uncacheable:        PAT2:  PAT = 0, PCD = 1, PWT = 0
         * uncacheable_strong: PAT3:  PAT = 0, PCD = 1, PWT = 1
         * write_protected:    PAT4:  PAT = 1, PCD = 0, PWT = 0
         * write_combining:    PAT5:  PAT = 1, PCD = 0, PWT = 1
         * none:               PAT6:  PAT = 1, PCD = 1, PWT = 0
         * none:               PAT7:  PAT = 1, PCD = 1, PWT = 1
        */

        const auto pat = (psize == page_size::small) ? arch::flag::pat : arch::flag::lpat;
        switch (cache)
        {
            case caching::uncacheable:
                ret |= arch::flag::pcd;
                break;
            case caching::uncacheable_strong:
            case caching::device:
                ret |= arch::flag::pcd | arch::flag::pwt;
                break;
            case caching::write_through:
                ret |= arch::flag::pwt;
                break;
            case caching::write_protected:
                ret |= pat;
                break;
            case caching::write_combining:
                ret |= pat | arch::flag::pwt;
                break;
            case caching::write_back:
                break;
            default:
                std::unreachable();
        }
        return ret;
    }

    auto pagemap::from_arch(std::uintptr_t flags, page_size psize) -> std::pair<pflag, caching>
    {
        auto ret = pflag::exec;
        {
            if (flags & arch::flag::present)
                ret |= pflag::read;
            if (flags & arch::flag::write)
                ret |= pflag::write;
            if (flags & arch::flag::user)
                ret |= pflag::user;
            if (flags & arch::flag::global)
                ret |= pflag::global;
            if (flags & arch::flag::no_exec)
                ret &= ~pflag::exec;
        }

        auto cache = caching::normal;
        {
            bool is_pat = (psize == page_size::small) ? (flags & arch::flag::pat) : (flags & arch::flag::lpat);
            bool is_pcd = (flags & arch::flag::pcd);
            bool is_pwt = (flags & arch::flag::pwt);

            if (!is_pat && !is_pcd && !is_pwt)
                cache = caching::write_back;
            else if (!is_pat && !is_pcd && is_pwt)
                cache = caching::write_through;
            else if (!is_pat && is_pcd && !is_pwt)
                cache = caching::uncacheable;
            else if (!is_pat && is_pcd && is_pwt)
                cache = caching::uncacheable_strong;
            else if (is_pat && !is_pcd && !is_pwt)
                cache = caching::write_protected;
            else if (is_pat && !is_pcd && is_pwt)
                cache = caching::write_combining;
        }

        return { ret, cache };
    }

    auto pagemap::get_arch_table(std::uintptr_t addr) const -> table *
    {
        lib::unused(addr);
        return _table;
    }

    [[gnu::pure]] bool pagemap::is_canonical(std::uintptr_t addr)
    {
        return (addr < 0x0000800000000000) || (addr >= 0xFFFF800000000000);
    }

    [[gnu::pure]] std::pair<std::uintptr_t, std::uintptr_t> pagemap::user_range()
    {
        return { 0x0000000000001000, 0x0000800000000000 };
    }

    [[gnu::pure]] std::pair<std::uintptr_t, std::uintptr_t> pagemap::kernel_range()
    {
        return { 0xFFFF800000000000, 0xFFFFFFFFFFFFFFFF };
    }

    [[gnu::pure]] std::size_t pagemap::from_page_size(page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(psize));
        return arch::page_sizes[std::to_underlying(psize)];
    }

    [[gnu::pure]] page_size pagemap::max_page_size(std::size_t size)
    {
        if (size > arch::page_sizes[2])
            return page_size::large;
        else if (size > arch::page_sizes[1])
            return page_size::medium;

        return page_size::small;
    }

    void pagemap::load() const
    {
        const auto addr = reinterpret_cast<std::uintptr_t>(_table);
        asm volatile ("mov cr3, %0" :: "r"(addr) : "memory");
    }

    pagemap::pagemap() : _table { new_table() }
    {
        if (!kernel_pagemap.valid())
        {
            // assume currently initialising the kernel_pagemap
            std::uint32_t a, b, c, d;
            arch::large_pages = cpu::id(0x80000001, 0, a, b, c, d) && (d & (1 << 26));

            // pre-allocate higher half
            auto table = lib::tohh(_table);
            for (std::size_t i = 256; i < 512; i++)
                getlvl(table->entries[i], true);
        }
        else
        {
            auto table = lib::tohh(_table);
            const auto ktable = lib::tohh(kernel_pagemap->_table);
            std::memcpy(table->entries + 256, ktable->entries + 256, 256 * sizeof(entry));
        }
    }
} // namespace vmm