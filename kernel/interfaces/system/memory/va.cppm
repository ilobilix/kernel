// Copyright (C) 2024-2026  ilobilo

export module system.memory.va;

import system.memory.phys;
import lib;
import std;

export namespace vmm::va
{
    struct range_t
    {
        std::uintptr_t startp;
        std::uintptr_t endp;

        lib::rbtree_hook<range_t> hook;
        lib::interval_hook<std::uintptr_t> interval;
    };

    std::optional<std::uintptr_t> find_free_region(
        const auto &tree, std::uintptr_t minp, std::uintptr_t maxp,
        std::uintptr_t pages
    )
     {
        if (pages == 0 || pages > maxp - minp)
            return std::nullopt;

        if (tree.empty())
            return maxp - pages;

        const auto root = tree.root();
        const auto nil = tree.nil();

        const auto high_gap = maxp - root->interval.subtree_max;
        const auto max_gap = root->interval.subtree_max_gap;
        const auto low_gap = root->interval.subtree_min > minp
            ? root->interval.subtree_min - minp : 0;

        if (max_gap < pages && high_gap < pages && low_gap < pages)
            return std::nullopt;

        if (high_gap >= pages)
            return maxp - pages;

        auto node = root;
        std::uintptr_t floor = minp;
        while (node != nil)
        {
            const auto left = node->hook.left;
            const auto right = node->hook.right;

            if (right != nil && right->interval.subtree_max_gap >= pages)
            {
                auto current_max = node->endp;
                if (left != nil && left->interval.subtree_max > current_max)
                    current_max = left->interval.subtree_max;

                if (current_max > floor)
                    floor = current_max;

                node = right;
                continue;
            }

            if (right != nil)
            {
                const auto gap_after = right->interval.subtree_min - node->endp;
                if (gap_after >= pages)
                    return right->interval.subtree_min - pages;
            }

            const auto prev_endp = std::max(floor, left != nil ? left->interval.subtree_max : 0ul);
            if (node->startp > prev_endp && (node->startp - prev_endp) >= pages)
                return node->startp - pages;

            if (left == nil)
                break;

            node = left;
        }

        const auto first = tree.first();
        if (first->startp > minp && (first->startp - minp) >= pages)
            return first->startp - pages;

        return std::nullopt;
    }

    class allocator
    {
        private:
        lib::interval_tree<
            range_t,
            std::uintptr_t,
            &range_t::startp,
            &range_t::endp,
            &range_t::hook,
            &range_t::interval
        > _tree;
        std::uintptr_t _minp;
        std::uintptr_t _maxp;
        lib::spinlock _lock;

        public:
        allocator(std::uintptr_t minp, std::uintptr_t maxp)
            : _minp { minp }, _maxp { maxp }
        {
            lib::bug_on(minp >= maxp);
        }

        std::optional<std::uintptr_t> allocate(std::size_t pages);
        bool free(std::uintptr_t startp, std::size_t pages);
    };
} // export namespace vmm::va
