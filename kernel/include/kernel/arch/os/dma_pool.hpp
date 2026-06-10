// Copyright (C) 2024-2026  ilobilo

#pragma once

#include <arch/dma_structs.hpp>
#include <cstdint>

namespace arch
{
    struct contiguous_pool_options
    {
        std::size_t min_gap = 64;
        bool sub4gib = false;
    };

    struct contiguous_pool : dma_pool
    {
        struct region : dma_region
        {
            region(contiguous_pool *pool, std::uintptr_t vaddr)
                : dma_region { pool } { base_va = vaddr; }
        };

        const contiguous_pool_options _opts;
        contiguous_pool(contiguous_pool_options opts = { })
            : _opts { opts } { }

        dma_ptr allocate(size_t size, size_t count, size_t align) override;
        void deallocate(dma_ptr ptr, size_t size, size_t count, size_t align) override;
    };
} // namespace arch
