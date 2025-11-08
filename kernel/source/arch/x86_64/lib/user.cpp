// Copyright (C) 2024-2025  ilobilo

module;

#include <user.h>

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
} // namespace lib