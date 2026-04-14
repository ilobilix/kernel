// Copyright (C) 2024-2025  ilobilo

module lib;

import system.cpu;
import cppstd;

namespace lib::impl
{
    void user_acquire()
    {
        cpu::smap::disable();
    }

    void user_release()
    {
        cpu::smap::enable();
    }

    void copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        user_acquire();
        std::memcpy(remove_user_cast<void>(dest), src, len);
        user_release();
    }

    void copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        user_acquire();
        std::memcpy(dest, remove_user_cast<void>(src), len);
        user_release();
    }

    void fill_user(void __user *dest, int value, std::size_t len)
    {
        user_acquire();
        std::memset(remove_user_cast<void>(dest), value, len);
        user_release();
    }

    std::size_t strnlen_user(const char __user *str, std::size_t len)
    {
        user_acquire();
        std::size_t ret = std::strnlen(remove_user_cast<const char>(str), len);
        user_release();
        return ret;
    }
} // namespace lib::impl