// Copyright (C) 2024  ilobilo

export module system.memory;

export import system.memory.phys;
export import system.memory.virt;
export import system.memory.slab;

export namespace memory
{
    void init()
    {
        pmm::init();
        slab::init();
        vmm::init();
    }
} // export namespace memory