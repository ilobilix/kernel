// Copyright (C) 2024  ilobilo

module;

#include <parallel_hashmap/btree.h>

export module lib:btree;

export namespace lib::btree
{
    template<typename Key, typename Compare = phmap::Less<Key>, typename Alloc = phmap::Allocator<Key>>
    using set = phmap::btree_set<Key, Compare, Alloc>;

    template<typename Key, typename Compare = phmap::Less<Key>, typename Alloc = phmap::Allocator<Key>>
    using multiset = phmap::btree_multiset<Key, Compare, Alloc>;

    template<typename Key, typename Value, typename Compare = phmap::Less<Key>, typename Alloc = phmap::Allocator<phmap::priv::Pair<const Key, Value>>>
    using map = phmap::btree_map<Key, Value, Compare, Alloc>;

    template<typename Key, typename Value, typename Compare = phmap::Less<Key>, typename Alloc = phmap::Allocator<phmap::priv::Pair<const Key, Value>>>
    using multimap = phmap::btree_multimap<Key, Value, Compare, Alloc>;
} // export namespace lib::btree