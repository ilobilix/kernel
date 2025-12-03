// Copyright (C) 2024-2025  ilobilo

import lib;
import std;

extern "C"
{
    void *malloc(std::size_t size)
    {
        return lib::alloc(size);
    }

    void *calloc(std::size_t num, std::size_t size)
    {
        return lib::allocz(num * size);
    }

    void *realloc(void *oldptr, std::size_t size)
    {
        return lib::realloc(oldptr, size);
    }

    void free(void *ptr)
    {
        lib::free(ptr);
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