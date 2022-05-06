// Copyright (C) 2024  ilobilo

import system.memory.slab;
import lib;
import std;

extern "C"
{
    void *malloc(std::size_t size)
    {
        // return slab::alloc(size);
        return std::calloc(1, size);
    }

    void *calloc(std::size_t num, std::size_t size)
    {
        auto ptr = slab::alloc(num * size);
        return std::memset(ptr, 0, num * size);
    }

    void *realloc(void *oldptr, std::size_t size)
    {
        return slab::realloc(oldptr, size);
    }

    void free(void *ptr)
    {
        slab::free(ptr);
    }

    int atoi(const char *str)
    {
        return lib::str2int<long>(str, nullptr, 10);
    }

    long atol(const char *str)
    {
        return lib::str2int<long>(str, nullptr, 10);
    }

    long long atoll(const char *str)
    {
        return lib::str2int<long long>(str, nullptr, 10);
    }

    long strtol(const char *str, char **str_end, int base)
    {
        return lib::str2int<long>(str, str_end, base);
    }

    long long strtoll(const char *str, char **str_end, int base)
    {
        return lib::str2int<long long>(str, str_end, base);
    }

    unsigned long strtoul(const char *str, char **str_end, int base)
    {
        return lib::str2int<unsigned long>(str, str_end, base);
    }

    unsigned long long strtoull(const char *str, char **str_end, int base)
    {
        return lib::str2int<unsigned long long>(str, str_end, base);
    }
} // extern "C"