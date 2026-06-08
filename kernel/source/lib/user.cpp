// Copyright (C) 2024-2026  ilobilo

module lib;

import system.memory.virt;
import std;

namespace lib
{
    address_space classify_address(std::uintptr_t addr, std::size_t len)
    {
        const auto [user_start, user_end] = vmm::pagemap::user_range();
        const auto [kernel_start, kernel_end] = vmm::pagemap::kernel_range();

        if (addr >= user_start && addr < user_end && len <= user_end - addr)
            return address_space::user;
        else if (addr >= kernel_start && len <= kernel_end - addr)
            return address_space::kernel;
        return address_space::invalid;
    }

    bool copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(dest), len);
        if (space != address_space::user)
            return false;
        return impl::copy_to_user(dest, src, len);
    }

    bool copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(src), len);
        if (space != address_space::user)
            return false;
        return impl::copy_from_user(dest, src, len);
    }

    bool fill_user(void __user *dest, int value, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(dest), len);
        if (space != address_space::user)
            return false;
        return impl::fill_user(dest, value, len);
    }

    bool cmpxchg_user(std::uint32_t __user *uaddr, std::uint32_t &expected, std::uint32_t desired)
    {
        const auto space =
            classify_address(reinterpret_cast<std::uintptr_t>(uaddr), sizeof(std::uint32_t));
        if (space != address_space::user)
            return false;
        return impl::cmpxchg_user(uaddr, expected, desired);
    }

    std::ssize_t strnlen_user(const char __user *str, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(str), len);
        if (space != address_space::user)
            return -1;
        return impl::strnlen_user(str, len);
    }
} // namespace lib
