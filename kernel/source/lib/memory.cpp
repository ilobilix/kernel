// Copyright (C) 2024-2025  ilobilo

module lib;

import system.memory.slab;
import std;

namespace lib::detail
{
    void *alloc(std::size_t size)
    {
        return slab::alloc(size);
    }

    void *allocz(std::size_t size)
    {
        auto ptr = slab::alloc(size);
        std::memset(ptr, 0, size);
        return ptr;
    }

    void *realloc(void *oldptr, std::size_t size)
    {
        return slab::realloc(oldptr, size);
    }

    void free(void *ptr)
    {
        slab::free(ptr);
    }
} // namespace lib::detail