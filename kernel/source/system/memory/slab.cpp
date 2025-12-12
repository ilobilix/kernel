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
            // return lib::tohh(pmm::alloc(lib::div_roundup(length, pmm::page_size)));
            const auto pages = lib::div_roundup(length, pmm::page_size);
            const auto vaddr = vmm::alloc_vspace(pages);

            const auto psize = vmm::page_size::small;
            const auto flags = vmm::pflag::rwg;

            for (std::size_t i = 0; i < pages; i++)
            {
                const auto paddr = pmm::alloc(1, true);
                const auto ret = vmm::kernel_pagemap->map(
                    vaddr + i * pmm::page_size, paddr,
                    pmm::page_size, flags, psize,
                    vmm::caching::normal
                );
                lib::panic_if(!ret,
                    "slab: could not map page: {}",
                    magic_enum::enum_name(ret.error())
                );
            }

            return vaddr;
        }

        void unmap(std::uintptr_t addr, std::size_t length)
        {
            // pmm::free(lib::fromhh(addr), lib::div_roundup(length, pmm::page_size));
            for (std::size_t offset = 0; offset < length; offset += pmm::page_size)
            {
                const auto vaddr = addr + offset;
                const auto ret = vmm::kernel_pagemap->translate(vaddr, vmm::page_size::small);
                lib::panic_if(!ret,
                    "slab: could not translate page: {}",
                    magic_enum::enum_name(ret.error())
                );

                const auto paddr = ret.value();
                const auto uret = vmm::kernel_pagemap->unmap(vaddr, pmm::page_size, vmm::page_size::small);
                lib::panic_if(!uret,
                    "slab: could not unmap page: {}",
                    magic_enum::enum_name(uret.error())
                );

                pmm::free(paddr, 1);
            }
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
        lib::info("heap: initialising the slab allocator");

        pool.initialize(valloc);
        kalloc.initialize(pool.get());
    }
} // namespace slab