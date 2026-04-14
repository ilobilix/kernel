// Copyright (C) 2024-2025  ilobilo

module lib;

import system.cpu;
import cppstd;

namespace lib
{
    void copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        cpu::smap::as_user([&] {
            std::memcpy((__force void *)dest, src, len);
        });
    }

    void copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        cpu::smap::as_user([&] {
            std::memcpy(dest, (__force void *)src, len);
        });
    }

    void strncpy_from_user(char *dest, const char __user *src, std::size_t len)
    {
        cpu::smap::as_user([&] {
            std::strncpy(dest, (__force const char *)src, len);
        });
    }

    std::size_t strnlen_user(const char __user *str, std::size_t len)
    {
        std::size_t ret = 0;
        cpu::smap::as_user([&] {
            ret = std::strnlen((__force const char *)str, len);
        });
        return ret;
    }
} // namespace lib