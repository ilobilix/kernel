// Copyright (C) 2024-2025  ilobilo

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif
    void *malloc(size_t size);
    void *calloc(size_t num, size_t size);
    void *realloc(void *oldptr, size_t size);
    void free(void *ptr);

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
