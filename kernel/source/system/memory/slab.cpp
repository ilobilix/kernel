// Copyright (C) 2022-2024  ilobilo

module system.memory.slab;

import system.memory.phys;
import lib;
import std;

namespace slab
{
    namespace
    {
        // TODO: use 1 here or don't depend on n contiguous pages being available
        constexpr std::size_t slab_pages = 2;

        struct slab;
        using metadata = slab *;

        struct lmetadata
        {
            std::size_t pages;
            std::size_t pages_nm;
            std::size_t size;
        };

        struct slab
        {
            std::mutex _lock;
            std::size_t _size;
            std::uintptr_t _start;

            void *alloc()
            {
                std::unique_lock _ { _lock };

                if (_start == 0)
                    init(_size);

                auto old = reinterpret_cast<std::uintptr_t *>(_start);
                _start = *old;
                return reinterpret_cast<void *>(old);
            }

            void free(void *ptr)
            {
                if (ptr == nullptr)
                    return;

                std::unique_lock _ { _lock };

                *static_cast<std::uintptr_t *>(ptr) = _start;
                _start = reinterpret_cast<std::uintptr_t>(ptr);
            }

            void init(std::size_t size)
            {
                _size = size;
                _start = lib::tohh(pmm::alloc<std::uintptr_t>(slab_pages));

                const auto aligned_size = lib::align_up(sizeof(metadata), _size);
                const auto available = (pmm::page_size * slab_pages) - aligned_size;
                const auto max = (available / _size) - 1;
                const auto factor = _size / 8;

                auto ptr = reinterpret_cast<metadata *>(_start);
                *ptr = this;
                _start += aligned_size;

                auto array = reinterpret_cast<std::uintptr_t *>(_start);

                for (std::size_t i = 0; i < max; i++)
                    array[i * factor] = reinterpret_cast<std::uintptr_t>(array[(i + 1) * factor]);
                array[max * factor] = 0;
            }
        };

        std::array<slab, 12> slabs;

        std::optional<std::reference_wrapper<slab>> find(std::size_t size)
        {
            for (auto &slab : slabs)
            {
                if (slab._size >= size)
                    return std::ref(slab);
            }
            return std::nullopt;
        }

        bool is_large(void *ptr)
        {
            // small slabs are never page aligned
            return (reinterpret_cast<std::uintptr_t>(ptr) & 0xFFF) == 0;
        }
    } // namespace

    void *alloc(std::size_t size)
    {
        auto slab = find(size);
        if (slab == std::nullopt)
        {
            auto pages = lib::div_roundup(size, pmm::page_size);
            auto ptr = lib::tohh(pmm::alloc(pages + 1));

            *reinterpret_cast<lmetadata *>(ptr) = lmetadata { pages + 1, pages, size };
            return reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(ptr) + pmm::page_size);
        }

        return slab->get().alloc();
    }

    void *realloc(void *oldptr, std::size_t size)
    {
        if (oldptr == nullptr)
            return alloc(size);

        std::size_t oldsize = size;

        if (is_large(oldptr))
        {
            auto metadata = reinterpret_cast<lmetadata *>(reinterpret_cast<std::uintptr_t>(oldptr) - pmm::page_size);
            oldsize = metadata->size;

            if (lib::div_roundup(oldsize, pmm::page_size) == lib::div_roundup(size, pmm::page_size))
            {
                metadata->size = lib::div_roundup(oldsize, pmm::page_size);
                return oldptr;
            }
        }
        else
        {
            auto slabptr = *reinterpret_cast<metadata *>(reinterpret_cast<std::uintptr_t>(oldptr) & ~0xFFF);
            oldsize = slabptr->_size;
        }

        if (size == 0)
        {
            free(oldptr);
            return nullptr;
        }

        if (size < oldsize)
            oldsize = size;

        void *newptr = alloc(size);
        if (newptr == nullptr)
        {
            free(oldptr);
            return nullptr;
        }

        std::memcpy(newptr, oldptr, oldsize);
        free(oldptr);
        return newptr;
    }

    void free(void *ptr)
    {
        if (ptr == nullptr)
            return;

        if (is_large(ptr))
        {
            auto metadata = reinterpret_cast<lmetadata *>(reinterpret_cast<std::uintptr_t>(ptr) - pmm::page_size);
            pmm::free(lib::fromhh(metadata), metadata->pages);
        }
        else
        {
            auto slabptr = *reinterpret_cast<metadata *>(reinterpret_cast<std::uintptr_t>(ptr) & ~0xFFF);
            slabptr->free(ptr);
        }
    }

    void init()
    {
        log::info("Initialising the slab allocator");

        slabs[0].init(8);
        slabs[1].init(16);
        slabs[2].init(32);
        slabs[3].init(48);
        slabs[4].init(80);
        slabs[5].init(128);
        slabs[6].init(192);
        slabs[7].init(288);
        slabs[8].init(448);
        slabs[9].init(672);
        slabs[10].init(1024);
        slabs[11].init(2048);
    }
} // namespace slab