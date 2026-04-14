// Copyright (C) 2024-2025  ilobilo

export module system.memory.slab;
import std;

export namespace slab
{
    void *alloc(std::size_t size);
    void *realloc(void *oldptr, std::size_t size);
    void free(void *ptr);

    void init();
} // export namespace slab