// Copyright (C) 2024-2025  ilobilo

module lib;

import system.memory.virt;
import std;

namespace lib
{
    address_space classify_address(std::uintptr_t addr, std::size_t len)
    {
        static const auto [user_start, user_end] = vmm::pagemap::user_range();
        static const auto [kernel_start, kernel_end] = vmm::pagemap::kernel_range();
        if (addr >= user_start && (addr + len) <= user_end)
            return address_space::user;
        else if (addr >= kernel_start && (addr + len) <= kernel_end)
            return address_space::kernel;
        return address_space::invalid;
    }

    bool copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(dest), len);
        if (space != address_space::user)
            return false;
        impl::copy_to_user(dest, src, len);
        return true;
    }

    bool copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(src), len);
        if (space != address_space::user)
            return false;
        impl::copy_from_user(dest, src, len);
        return true;
    }

    bool fill_user(void __user *dest, int value, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(dest), len);
        if (space != address_space::user)
            return false;
        impl::fill_user(dest, value, len);
        return true;
    }

    std::ssize_t strnlen_user(const char __user *str, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(str), len);
        if (space != address_space::user)
            return -1;
        return impl::strnlen_user(str, len);
    }

    bool maybe_copy_to_user(void *dest, const void *src, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(dest), len);
        switch (space)
        {
            case address_space::user:
                impl::copy_to_user(add_user_cast<void>(dest), src, len);
                return true;
            case address_space::kernel:
                std::memcpy(dest, src, len);
                return true;
            case address_space::invalid:
                return (errno = EFAULT, false);
        }
        std::unreachable();
    }

    bool maybe_copy_from_user(void *dest, const void *src, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(src), len);
        switch (space)
        {
            case address_space::user:
                impl::copy_from_user(dest, add_user_cast<const void>(src), len);
                return true;
            case address_space::kernel:
                std::memcpy(dest, src, len);
                return true;
            case address_space::invalid:
                return (errno = EFAULT, false);
        }
        std::unreachable();
    }

    bool maybe_fill_user(void *dest, int value, std::size_t len)
    {
        const auto space = classify_address(reinterpret_cast<std::uintptr_t>(dest), len);
        switch (space)
        {
            case address_space::user:
                impl::fill_user(add_user_cast<void>(dest), value, len);
                return true;
            case address_space::kernel:
                std::memset(dest, value, len);
                return true;
            case address_space::invalid:
                return (errno = EFAULT, false);
        }
        std::unreachable();
    }
} // namespace lib