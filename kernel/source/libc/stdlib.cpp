// Copyright (C) 2024-2026  ilobilo

import lib;
import std;

extern "C"
{
    void *malloc(std::size_t size) { return lib::alloc(size); }

    void *calloc(std::size_t num, std::size_t size) { return lib::allocz(num * size); }

    void *realloc(void *oldptr, std::size_t size) { return lib::realloc(oldptr, size); }

    void free(void *ptr) { lib::free(ptr); }

    int atoi(const char *str)
    {
        constexpr auto max = std::numeric_limits<int>::max();
        return lib::str2int<int>(str, nullptr, 10).value_or(max);
    }

    long atol(const char *str)
    {
        constexpr auto max = std::numeric_limits<long>::max();
        return lib::str2int<long>(str, nullptr, 10).value_or(max);
    }

    long long atoll(const char *str)
    {
        constexpr auto max = std::numeric_limits<long long>::max();
        return lib::str2int<long long>(str, nullptr, 10).value_or(max);
    }

    long strtol(const char *str, char **str_end, int base)
    {
        constexpr auto max = std::numeric_limits<long>::max();
        return lib::str2int<long>(str, str_end, base).value_or(max);
    }

    long long strtoll(const char *str, char **str_end, int base)
    {
        constexpr auto max = std::numeric_limits<long long>::max();
        return lib::str2int<long long>(str, str_end, base).value_or(max);
    }

    unsigned long strtoul(const char *str, char **str_end, int base)
    {
        constexpr auto max = std::numeric_limits<unsigned long>::max();
        return lib::str2int<unsigned long>(str, str_end, base).value_or(max);
    }

    unsigned long long strtoull(const char *str, char **str_end, int base)
    {
        constexpr auto max = std::numeric_limits<unsigned long long>::max();
        return lib::str2int<unsigned long long>(str, str_end, base).value_or(max);
    }
} // extern "C"
