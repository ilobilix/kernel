// Copyright (C) 2024-2025  ilobilo

#pragma once

namespace std
{
    template<typename Type>
    struct allocator
    {
        using value_type = Type;
        using pointer = Type *;
        using const_pointer = const Type *;
        using reference = Type &;
        using const_reference = const Type &;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        constexpr allocator() noexcept = default;

        template<typename U>
        constexpr allocator(const allocator<U> &) noexcept { }

        [[nodiscard]] constexpr Type *allocate(std::size_t count)
        {
            return static_cast<Type *>(::operator new(count * sizeof(Type)));
        }

        constexpr void deallocate(Type *ptr, std::size_t count)
        {
            ::operator delete(ptr, count * sizeof(Type));
        }

        friend constexpr bool operator==(const allocator &, const allocator &) noexcept
        {
            return true;
        }
    };

    template<>
    struct allocator<void>
    {
        using value_type = void;
        using pointer = void *;
        using const_pointer = const void *;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        constexpr allocator() noexcept = default;

        template<typename U>
        constexpr allocator(const allocator<U> &) noexcept { }
    };
} // namespace std