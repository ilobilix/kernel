// Copyright (C) 2024-2025  ilobilo

module;

#include <user.h>

export module lib:user;
import cppstd;

export namespace lib
{
    void copy_to_user(void __user *dest, const void *src, std::size_t len);
    void copy_from_user(void *dest, const void __user *src, std::size_t len);

    void strncpy_from_user(char *dest, const char __user *src, std::size_t len);
    std::size_t strnlen_user(const char __user *str, std::size_t len);
} // export namespace lib