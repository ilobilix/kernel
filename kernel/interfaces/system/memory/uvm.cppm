// Copyright (C) 2024-2025  ilobilo

export module system.memory.virt:uvm;

import :pagemap;
import cppstd;

namespace vmm
{
    export enum class prot
    {
        none = 0x00,
        read = 0x01,
        write = 0x02,
        exec = 0x04
    };

    export enum map_flag
    {
        failed = -1,
        file = 0x00,
        shared = 0x01,
        private_ = 0x02,
        fixed = 0x10,
        anonymous = 0x20
    };

    export struct vmspace
    {
        std::shared_ptr<pagemap> pmap;
    };
} // export namespace vmm