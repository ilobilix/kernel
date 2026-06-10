// Copyright (C) 2024-2026  ilobilo

#include <arch/dma_pool.hpp>

import system.memory.phys;
import lib;

namespace arch
{
    dma_ptr contiguous_pool::allocate(size_t size, size_t count, size_t align)
    {
        lib::unused(align);
        const auto pages = lib::div_roundup(size * count, pmm::page_size);
        const auto type = _opts.sub4gib ? pmm::type::sub4gib : pmm::type::normal;
        const auto addr = pmm::alloc(pages, true, type);
        return { new region { this, lib::tohh(addr) }, 0 };
    }

    void contiguous_pool::deallocate(dma_ptr ptr, size_t size, size_t count, size_t align)
    {
        lib::unused(align);
        const auto pages = lib::div_roundup(size * count, pmm::page_size);
        pmm::free(lib::fromhh(ptr.region()->get_base_va()), pages);
    }
} // namespace arch
