// Copyright (C) 2024-2025  ilobilo

module;

#include <parallel_hashmap/phmap.h>

export module lib:set;

export namespace lib::set
{
    template<
        typename Type,
        typename Hash = phmap::priv::hash_default_hash<Type>,
        typename Eq = phmap::priv::hash_default_eq<Type>,
        typename Alloc = phmap::priv::Allocator<Type>
    >
    using node_hash = phmap::node_hash_set<Type, Hash, Eq, Alloc>;

    template<
        typename Type,
        typename Hash = phmap::priv::hash_default_hash<Type>,
        typename Eq = phmap::priv::hash_default_eq<Type>,
        typename Alloc = phmap::priv::Allocator<Type>
    >
    using flat_hash = phmap::flat_hash_set<Type, Hash, Eq, Alloc>;

    template<
        typename Type,
        typename Hash = phmap::priv::hash_default_hash<Type>,
        typename Eq = phmap::priv::hash_default_eq<Type>,
        typename Alloc = phmap::priv::Allocator<Type>,
        std::size_t N = 4,
        typename Mutex = phmap::NullMutex
    >
    using parallel_node_hash = phmap::parallel_node_hash_set<Type, Hash, Eq, Alloc, N, Mutex>;

    template<
        typename Type,
        typename Hash = phmap::priv::hash_default_hash<Type>,
        typename Eq = phmap::priv::hash_default_eq<Type>,
        typename Alloc = phmap::priv::Allocator<Type>,
        std::size_t N = 4,
        typename Mutex = phmap::NullMutex
    >
    using parallel_flat_hash = phmap::parallel_flat_hash_set<Type, Hash, Eq, Alloc, N, Mutex>;
} // export namespace lib::set