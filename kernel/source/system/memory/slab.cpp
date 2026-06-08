// Copyright (C) 2024-2026  ilobilo

module system.memory.slab;

import system.memory.phys;
import system.memory.virt;
import frigg;
import lib;

namespace slab
{
    struct policy
    {
        // TODO: some issues on a certain laptop
        std::uintptr_t map(std::size_t length)
        {
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);

            const auto aligned = lib::align_up(length, npsize);
            const auto vaddr = vmm::alloc_vspace(aligned);

            const auto flags = vmm::pflag::rwg;

            for (std::uintptr_t i = vaddr; i < vaddr + aligned; i += npsize)
            {
                const auto paddr = pmm::alloc(npsize / pmm::page_size, true);
                const auto ret =
                    vmm::kernel_pagemap->map(i, paddr, npsize, flags, psize, vmm::caching::normal);
                if (!ret)
                    lib::panic("slab: could not map page: {}", lib::error_name(ret.error()));
            }

            return vaddr;
        }

        void unmap(std::uintptr_t addr, std::size_t length)
        {
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);

            const auto aligned = lib::align_up(length, npsize);
            // std::memset(reinterpret_cast<void *>(addr), 0xDE, aligned);

            for (std::uintptr_t i = addr; i < addr + aligned; i += npsize)
            {
                const auto ret = vmm::kernel_pagemap->translate(i, psize);
                if (!ret)
                    lib::panic("slab: could not translate page: {}", lib::error_name(ret.error()));

                const auto paddr = *ret;
                if (const auto uret = vmm::kernel_pagemap->unmap(i, npsize, psize); !uret)
                    lib::panic("slab: could not unmap page: {}", lib::error_name(uret.error()));

                pmm::free(paddr, npsize / pmm::page_size);
            }

            vmm::free_vspace(addr, aligned);
        }
    };

    constinit policy valloc;
    constinit frg::manual_box<frg::slab_pool<policy, lib::spinlock_irq>> pool;
    constinit frg::manual_box<frg::slab_allocator<policy, lib::spinlock_irq>> kalloc;

    void *alloc(std::size_t size) { return kalloc->allocate(size); }

    void *realloc(void *oldptr, std::size_t size) { return kalloc->reallocate(oldptr, size); }

    void free(void *ptr) { return kalloc->free(ptr); }

    void init()
    {
        lib::info("heap: initialising the slab allocator");

        pool.initialize(valloc);
        kalloc.initialize(pool.get());
    }
} // namespace slab
