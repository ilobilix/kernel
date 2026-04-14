// Copyright (C) 2024  ilobilo

#pragma once

#include <stddef.h>

#ifdef __cplusplus
namespace cdecl
{
    extern "C"
    {
#endif
        void *malloc(size_t size);
        void *calloc(size_t num, size_t size);
        void *realloc(void *oldptr, size_t size);
        void free(void *ptr);
#ifdef __cplusplus
    } // extern "C"
} // namespace cdecl

extern "C++"
{
    template<typename Type = void *>
    inline Type malloc(size_t size)
    {
        return reinterpret_cast<Type>(cdecl::malloc(size));
    }

    template<typename Type = void *>
    inline Type calloc(size_t num, size_t size)
    {
        return reinterpret_cast<Type>(cdecl::calloc(num, size));
    }

    template<typename Type>
    inline Type realloc(Type oldptr, size_t size)
    {
        return reinterpret_cast<Type>(cdecl::realloc(reinterpret_cast<void *>(oldptr), size));
    }

    inline void free(auto ptr)
    {
        cdecl::free(reinterpret_cast<void *>(ptr));
    }
} // extern "C++"

extern "C"
{
#endif
    int atoi(const char *str);
    long atol(const char *str);
    long long atoll(const char *str);

    long strtol(const char *str, char **str_end, int base);
    long long strtoll(const char *str, char **str_end, int base);

    unsigned long strtoul(const char *str, char **str_end, int base);
    unsigned long long strtoull(const char *str, char **str_end, int base);
#ifdef __cplusplus
} // extern "C"
#endif
