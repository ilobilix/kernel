// Copyright (C) 2024-2025  ilobilo

module;

#include <frg/rbtree.hpp>
#include <frg/pairing_heap.hpp>
#include <frg/intrusive.hpp>
#include <frg/small_vector.hpp>
#include <frg/manual_box.hpp>
#include <frg/slab.hpp>
#include <frg/list.hpp>

export module frigg;
import std;

export namespace frg
{
    using ::frg::pairing_heap;
    using ::frg::pairing_heap_hook;
    using ::frg::rbtree;
    using ::frg::rbtree_hook;
    using ::frg::intrusive_list;
    using ::frg::locate_member;
    using ::frg::small_vector;
    using ::frg::manual_box;
    using ::frg::slab_pool;
    using ::frg::slab_allocator;

    using ::frg::default_list_hook;

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