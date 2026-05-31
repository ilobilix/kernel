// Copyright (C) 2024-2026  ilobilo

module lib;
import std;

namespace lib::impl
{
    bool copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        std::memcpy(remove_user_cast<void>(dest), src, len);
        return true;
    }

    bool copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        std::memcpy(dest, remove_user_cast<void>(src), len);
        return true;
    }

    bool fill_user(void __user *dest, int value, std::size_t len)
    {
        std::memset(remove_user_cast<void>(dest), value, len);
        return true;
    }

    bool cmpxchg_user(std::uint32_t __user *uaddr, std::uint32_t &expected, std::uint32_t desired)
    {
        auto ptr = remove_user_cast<std::uint32_t>(uaddr);
        return std::atomic_ref<std::uint32_t> { *ptr } .compare_exchange_strong(
            expected, desired,
            std::memory_order_acq_rel, std::memory_order_acquire
        );
    }

    std::ssize_t strnlen_user(const char __user *str, std::size_t len)
    {
        return std::strnlen(remove_user_cast<const char>(str), len);
    }
} // namespace lib::impl
