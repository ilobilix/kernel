// Copyright (C) 2024-2025  ilobilo

module;

#include <arch/aarch64/system/cpu.hpp>

module system.memory.virt;

import system.memory.phys;
import system.cpu;
import boot;
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
                valid = (1 << 0),
                table = (1 << 1),
                block = (0 << 1),
                page = (1 << 1),

                user = (1 << 6),

                read_write = (0 << 7),
                read_only = (1 << 7),

                access = (1 << 10),
                not_global = (1 << 11),
                exec_never = (1ul << 54),

                // non_share = (0b00 << 8),
                // out_share = (0b10 << 8),
                in_share = (0b11 << 8),
            };

            std::size_t page_sizes[]
            {
                0, // page_size::small
                0, // page_size::medium
                0  // page_size::large
            };
        } // namespace
    } // namespace arch

    std::uintptr_t pagemap::pa_mask;

    const std::uintptr_t pagemap::valid_table_flags = arch::valid | arch::table;
    const std::uintptr_t pagemap::new_table_flags = arch::valid | arch::table;

    bool pagemap::entry::accessor::is_large() const
    {
        return !(value & arch::page);
    }

    struct arch_table { pagemap::table *ttbr1; };
    arch_table accessor;

    // pagemap::table *ttbr1;

    auto pagemap::new_table() -> table *
    {
        static_assert(sizeof(table) == pmm::page_size);
        return reinterpret_cast<table *>(pmm::alloc(1, true));
    }

    void pagemap::free_table(table *ptr)
    {
        pmm::free(reinterpret_cast<std::uintptr_t>(ptr), 1);
    }

    page_size pagemap::fixpsize(page_size psize) { return psize; }
    void pagemap::invalidate(std::uintptr_t vaddr)
    {
        cpu::invlpg(vaddr);
    }

    std::uintptr_t pagemap::to_arch(pflag flags, caching cache, page_size psize)
    {
        lib::bug_on(!magic_enum::enum_contains(cache));
        lib::bug_on(!magic_enum::enum_contains(psize));

        std::uintptr_t ret = arch::flag::valid | arch::flag::access | arch::flag::in_share;

        if ((flags & pflag::write) == pflag::none)
            ret |= arch::flag::read_only;
        if ((flags & pflag::user) != pflag::none)
            ret |= arch::flag::user;
        if ((flags & pflag::global) == pflag::none)
            ret |= arch::flag::not_global;
        if ((flags & pflag::exec) == pflag::none)
            ret |= arch::flag::exec_never;

        if (psize == page_size::small)
            ret |= arch::flag::page;

        switch (cache)
        {
            case caching::uncacheable:
            case caching::uncacheable_strong:
                ret |= (1 << 2);
                break;
            case caching::device:
                ret |= (2 << 2);
                break;
            default:
                ret |= (0 << 2);
        }
        return ret;
    }

    auto pagemap::from_arch(std::uintptr_t flags, page_size psize) -> std::pair<pflag, caching>
    {
        lib::unused(psize);

        auto ret = pflag::none;
        {
            if ((flags & arch::flag::access) != 0)
                ret |= pflag::read;
            if ((flags & arch::flag::read_only) == 0)
                ret |= pflag::write;
            if ((flags & arch::flag::user) != 0)
                ret |= pflag::read;
            if ((flags & arch::flag::not_global) == 0)
                ret |= pflag::global;
            if ((flags & arch::flag::exec_never) == 0)
                ret |= pflag::exec;
        }

        auto cache = caching::normal;
        {
            if (flags & (1 << 2))
                cache = caching::uncacheable_strong;
            else if (flags & (2 << 2))
                cache = caching::device;
        }

        return { ret, cache };
    }

    auto pagemap::get_arch_table(std::uintptr_t addr) const -> table *
    {
        return (addr & (1ul << 63)) ? accessor.ttbr1 : _table;
    }

    bool pagemap::is_canonical(std::uintptr_t addr)
    {
        // TODO
        lib::unused(addr);
        return true;
    }

    [[gnu::pure]] std::pair<std::uintptr_t, std::uintptr_t> pagemap::user_range()
    {
        // TODO
        return { 0, 0 };
    }

    [[gnu::pure]] std::pair<std::uintptr_t, std::uintptr_t> pagemap::kernel_range()
    {
        // TODO
        return { 0, 0 };
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

    static std::size_t n = 0;
    static std::uint64_t tg0 = 0;

    void pagemap::load() const
    {
        if (n < cpu::count())
        {
            constexpr auto bits = [](std::size_t top, std::size_t bottom, std::uint64_t value)
            {
                std::uint64_t vbit = (1ul << ((top + 1) - bottom));
                return (value & (vbit - 1)) << bottom;
            };

            std::uint64_t sctlr_el1 = mrs(sctlr_el1);
            sctlr_el1 &= ~(1 << 1); // disable alignment check
            sctlr_el1 |= (1 << 2);
            sctlr_el1 |= (1 << 12);
            msr(sctlr_el1, sctlr_el1);

            std::uint64_t tcr_el1 = mrs(tcr_el1);
            tcr_el1 |= bits(34, 32, 0b011); // ips: 4 tib
            // ttbr1_el1
            tcr_el1 |= bits(29, 28, 0b11); // sh1: ish
            tcr_el1 |= bits(27, 26, 0b01); // orgn1: wbwa
            tcr_el1 |= bits(25, 24, 0b01); // irgn1: wbwa
            tcr_el1 |= bits(21, 16, 16); // t1sz: 48 bits
            // ttbr0_el1
            tcr_el1 |= bits(15, 14, tg0); // tg0
            tcr_el1 |= bits(13, 12, 0b11); // sh0: ish
            tcr_el1 |= bits(11, 10, 0b01); // orgn0: wbwa
            tcr_el1 |= bits(9, 8, 0b01); // irgn0: wbwa
            tcr_el1 |= bits(5, 0, 16); // t0sz: 48 bits
            msr(tcr_el1, tcr_el1);

            std::uint64_t mair_el1 = 0;
            mair_el1 |= (0xFF << (0 * 8)); // normal
            mair_el1 |= (0x40 << (1 * 8)); // non-cachable
            mair_el1 |= (0x00 << (2 * 8)); // nGnRnE
            msr(mair_el1, mair_el1);

            asm volatile ("msr ttbr1_el1, %0; isb; dsb sy; isb" :: "r" (reinterpret_cast<std::uintptr_t>(accessor.ttbr1)) : "memory");
            n++;
        }
        asm volatile ("msr ttbr0_el1, %0; isb; dsb sy; isb" :: "r" (reinterpret_cast<std::uintptr_t>(_table)) : "memory");
    }

    pagemap::pagemap() : _table { new_table() }
    {
        if (!kernel_pagemap.valid())
        {
            // assume currently initialising the kernel_pagemap
            accessor.ttbr1 = new_table();

            std::uint64_t tcr_el1 = mrs(tcr_el1);
            const auto tg1 = (tcr_el1 >> 30) & 0b11;
            std::size_t psize = 0;
            if (tg1 == 0b10)
            {
                psize = lib::kib(4);
                pa_mask = 0x0000FFFFFFFFF000;
                tg0 = 0b00;
            }
            else if (tg1 == 0b01)
            {
                psize = lib::kib(16);
                pa_mask = 0x0000FFFFFFFFC000;
                tg0 = 0b10;
            }
            else if (tg1 == 0b11)
            {
                psize = lib::kib(64);
                pa_mask = 0x0000FFFFFFFF0000;
                tg0 = 0b01;
            }
            else lib::panic("unknown page size");

            lib::debug("vmm: page size is 0x{:X} kib", psize);

            arch::page_sizes[0] = psize;
            arch::page_sizes[1] = arch::page_sizes[0] * 512;
            arch::page_sizes[2] = arch::page_sizes[1] * 512;

            // // pre-allocate higher half
            // auto table = lib::tohh(_table);
            // for (std::size_t i = 256; i < 512; i++)
            //     getlvl(ttbr1->entries[i], true);
        }
    }
} // namespace vmm