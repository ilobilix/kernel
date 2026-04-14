// Copyright (C) 2024-2025  ilobilo

export module system.memory.virt:mmap;

import system.cpu;
import system.vfs;
import :pagemap;
import cppstd;

export namespace vmm
{
    enum prot
    {
        none = 0x00,
        read = 0x01,
        write = 0x02,
        exec = 0x04
    };

    enum map
    {
        failed = -1,
        file = 0x00,
        shared = 0x01,
        private_ = 0x02,
        fixed = 0x10,
        anon = 0x20,
        anonymous = anon
    };

    // TODO: temporary
    class vspace
    {
        public:
        std::shared_ptr<vmm::pagemap> pmap;
    };
} // export namespace vmm