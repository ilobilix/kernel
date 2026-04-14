// Copyright (C) 2024-2026  ilobilo

module lib;

import system.cpu;
import std;

namespace lib::impl
{
    // TODO: handle aborts

    bool copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        cpu::smap::disable();
        std::memcpy(remove_user_cast<void>(dest), src, len);
        cpu::smap::enable();
        return true;
    }

    bool copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        cpu::smap::disable();
        std::memcpy(dest, remove_user_cast<void>(src), len);
        cpu::smap::enable();
        return true;
    }

    bool fill_user(void __user *dest, int value, std::size_t len)
    {
        cpu::smap::disable();
        std::memset(remove_user_cast<void>(dest), value, len);
        cpu::smap::enable();
        return true;
    }

    std::ssize_t strnlen_user(const char __user *str, std::size_t len)
    {
        cpu::smap::disable();
        const auto ret = std::strnlen(remove_user_cast<const char>(str), len);
        cpu::smap::enable();
        return ret;
    }
} // namespace lib::impl
