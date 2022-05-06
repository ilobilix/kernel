// Copyright (C) 2024  ilobilo

module;

#include <frg/pairing_heap.hpp>
#include <frg/intrusive.hpp>
#include <frg/small_vector.hpp>

export module frigg;
import std;

export namespace frg
{
    using ::frg::pairing_heap;
    using ::frg::pairing_heap_hook;
    using ::frg::locate_member;
    using ::frg::small_vector;

    template<typename Type>
    struct allocator : std::allocator<Type>
    {
        [[nodiscard]] constexpr Type *allocate(std::size_t size)
        {
            return static_cast<Type *>(::operator new(size));
        }

        constexpr void free(Type *ptr, std::size_t = 0)
        {
            ::operator delete(ptr);
        }
    };
} // export namespace frg