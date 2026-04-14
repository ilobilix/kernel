// Copyright (C) 2024-2025  ilobilo

export module system.memory;

export import system.memory.phys;
export import system.memory.virt;
export import system.memory.slab;

export namespace memory
{
    void init()
    {
        vmm::init();
        pmm::init();
        vmm::init_vspaces();
        slab::init();
    }
} // export namespace memory