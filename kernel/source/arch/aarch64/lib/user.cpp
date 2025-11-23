// Copyright (C) 2024-2025  ilobilo

module lib;
import cppstd;

namespace lib::impl
{
    void user_acquire() { }
    void user_release() { }

    void copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        std::memcpy((__force void *)dest, src, len);
    }

    void copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        std::memcpy(dest, (__force void *)src, len);
    }

    void fill_user(void __user *dest, int value, std::size_t len)
    {
        std::memset((__force void *)dest, value, len);
    }

    std::size_t strnlen_user(const char __user *str, std::size_t len)
    {
        return std::strnlen((__force const char *)str, len);
    }
} // namespace lib::impl