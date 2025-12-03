// Copyright (C) 2022-2024  ilobilo

module system.memory.slab;

import system.memory.phys;
import system.memory.virt;
import magic_enum;
import frigg;
import lib;
import std;

namespace slab
{
    struct policy
    {
        // TODO: some issues on a certain laptop
        std::uintptr_t map(std::size_t length)
        {
            // return lib::tohh(pmm::alloc<std::uintptr_t>(lib::div_roundup(length, pmm::page_size)));
            const auto pages = lib::div_roundup(length, pmm::page_size);
            const auto vaddr = vmm::alloc_vspace(pages);

            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::rwg;

            if (const auto ret = vmm::kernel_pagemap->map_alloc(vaddr, length, flags, psize); !ret)
                lib::panic("could not map slab memory: {}", magic_enum::enum_name(ret.error()));

            return vaddr;
        }

        void unmap(std::uintptr_t addr, std::size_t length)
        {
            // pmm::free(lib::fromhh(addr), lib::div_roundup(length, pmm::page_size));
            const auto psize = vmm::page_size::small;
            if (const auto ret = vmm::kernel_pagemap->unmap_dealloc(addr, length, psize); !ret)
                lib::panic("could not unmap slab memory: {}", magic_enum::enum_name(ret.error()));
        }
    };

    constinit policy valloc;
    constinit frg::manual_box<frg::slab_pool<policy, lib::spinlock>> pool;
    constinit frg::manual_box<frg::slab_allocator<policy, lib::spinlock>> kalloc;

    void *alloc(std::size_t size)
    {
        return kalloc->allocate(size);
    }

    void *realloc(void *oldptr, std::size_t size)
    {
        return kalloc->reallocate(oldptr, size);
    }

    void free(void *ptr)
    {
        return kalloc->free(ptr);
    }

    void init()
    {
        log::info("heap: initialising the slab allocator");

        pool.initialize(valloc);
        kalloc.initialize(pool.get());
    }
} // namespace slab