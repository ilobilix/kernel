// Copyright (C) 2024  ilobilo

module;

#include <parallel_hashmap/phmap.h>

export module lib:map;

export namespace lib::map
{
    template<
        typename Key, typename Value,
        typename Hash = phmap::priv::hash_default_hash<Key>,
        typename Eq = phmap::priv::hash_default_eq<Key>,
        typename Alloc = phmap::priv::Allocator<phmap::priv::Pair<const Key, Value>>
    >
    using node_hash = phmap::node_hash_map<Key, Value, Hash, Eq, Alloc>;

    template<
        typename Key, typename Value,
        typename Hash = phmap::priv::hash_default_hash<Key>,
        typename Eq = phmap::priv::hash_default_eq<Key>,
        typename Alloc = phmap::priv::Allocator<phmap::priv::Pair<const Key, Value>>
    >
    using flat_hash = phmap::flat_hash_map<Key, Value, Hash, Eq, Alloc>;

    template<
        typename Key, typename Value,
        typename Hash = phmap::priv::hash_default_hash<Key>,
        typename Eq = phmap::priv::hash_default_eq<Key>,
        typename Alloc = phmap::priv::Allocator<phmap::priv::Pair<const Key, Value>>,
        std::size_t N = 4,
        typename Mutex = phmap::NullMutex
    >
    using parallel_node_hash = phmap::parallel_node_hash_map<Key, Value, Hash, Eq, Alloc, N, Mutex>;

    template<
        typename Key, typename Value,
        typename Hash = phmap::priv::hash_default_hash<Key>,
        typename Eq = phmap::priv::hash_default_eq<Key>,
        typename Alloc = phmap::priv::Allocator<phmap::priv::Pair<const Key, Value>>,
        std::size_t N = 4,
        typename Mutex = phmap::NullMutex
    >
    using parallel_flat_hash = phmap::parallel_flat_hash_map<Key, Value, Hash, Eq, Alloc, N, Mutex>;
} // export namespace lib::map