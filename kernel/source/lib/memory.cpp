// Copyright (C) 2024-2026  ilobilo

module lib;

import system.memory.slab;
import std;

namespace lib::detail
{
    void *alloc(std::size_t size)
    {
        auto ptr = slab::alloc(size);
        if (!ptr) [[unlikely]]
            lib::panic("could not allocate {} bytes", size);
        return ptr;
    }

    void *allocz(std::size_t size)
    {
        auto ptr = alloc(size);
        std::memset(ptr, 0, size);
        return ptr;
    }

    void *realloc(void *oldptr, std::size_t size)
    {
        auto ptr = slab::realloc(oldptr, size);
        if (!ptr && size != 0) [[unlikely]]
            lib::panic("could not reallocate {} bytes", size);
        return ptr;
    }

    void free(void *ptr) { slab::free(ptr); }
} // namespace lib::detail
