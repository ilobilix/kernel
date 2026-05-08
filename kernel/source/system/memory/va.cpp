// Copyright (C) 2024-2026  ilobilo

module system.memory.va;

namespace vmm::va
{
    namespace
    {
        struct slot_t
        {
            alignas(range_t) std::byte storage[std::max(sizeof(range_t), sizeof(slot_t *))];
        };
        static_assert(std::is_trivially_destructible_v<range_t>);
        static_assert(sizeof(slot_t) <= 128);

        constexpr std::size_t slots_per_page = pmm::page_size / sizeof(slot_t);

        constinit slot_t *head = nullptr;
        constinit lib::spinlock lock;

        slot_t *&next_of(slot_t *slot)
        {
            return *reinterpret_cast<slot_t **>(&slot->storage);
        }

        range_t *pool_alloc()
        {
            const std::unique_lock _ { lock };
            if (!head)
            {
                const auto paddr = pmm::alloc();
                if (paddr == 0)
                    return nullptr;

                auto slots = reinterpret_cast<slot_t *>(lib::tohh(paddr));
                for (std::size_t i = 0; i < slots_per_page; i++)
                {
                    next_of(&slots[i]) = head;
                    head = &slots[i];
                }
            }

            auto slot = head;
            head = next_of(slot);
            return new (&slot->storage) range_t { };
        }

        void pool_free(range_t *range)
        {
            auto slot = reinterpret_cast<slot_t *>(range);
            const std::unique_lock _ { lock };
            next_of(slot) = head;
            head = slot;
        }
    } // namespace

    std::optional<std::uintptr_t> allocator::allocate(std::size_t pages)
    {
        if (pages == 0)
            return std::nullopt;

        const std::unique_lock _ { _lock };

        const auto page = find_free_region(_tree, _minp, _maxp, pages);
        if (!page)
            return std::nullopt;

        auto range = pool_alloc();
        if (!range)
            return std::nullopt;

        range->startp = *page;
        range->endp = *page + pages;
        _tree.insert(range);

        return *page;
    }

    bool allocator::free(std::uintptr_t startp, std::size_t pages)
    {
        if (pages == 0 || startp < _minp || startp >= _maxp)
            return false;

        const auto endp = startp + pages;
        const std::unique_lock _ { _lock };

        auto overlapping = _tree.overlapping(startp, endp);
        auto it = overlapping.begin();
        if (it == overlapping.end())
            return false;

        auto ent = it.value();
        if (ent->startp != startp || ent->endp != endp)
            return false;

        _tree.remove(ent);
        pool_free(ent);
        return true;
    }
} // namespace vmm::va
