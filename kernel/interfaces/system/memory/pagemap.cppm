// Copyright (C) 2024-2025  ilobilo

export module system.memory.virt:pagemap;

import magic_enum;
import frigg;
import lib;
import cppstd;

namespace vmm
{
    struct arch_table;
} // namespace vmm

export namespace vmm
{
    enum class caching
    {
        uncacheable,
        uncacheable_strong,
        write_through,
        write_protected,
        write_combining,
        write_back,
        device,

        normal = write_back,
        mmio = uncacheable_strong,
        framebuffer = write_combining
    };

    enum class pflag
    {
        none = 0,
        read = (1 << 0),
        write = (1 << 1),
        exec = (1 << 2),
        user = (1 << 3),
        global = (1 << 4),

        rw = read | write,
        rwx = read | write | exec,

        rwg = rw | global,
        rwxg = rwx | global,

        rwu = rw | user,
        rwxu = rwx | user
    };

    enum class page_size
    {
        // do not modify
        small,
        medium,
        large
    };

    enum class error
    {
        addr_not_aligned,
        addr_in_use,
        not_mapped,
        invalid_entry
    };

    using magic_enum::bitwise_operators::operator~;
    using magic_enum::bitwise_operators::operator&;
    using magic_enum::bitwise_operators::operator&=;
    using magic_enum::bitwise_operators::operator|;
    using magic_enum::bitwise_operators::operator|=;

    class pagemap
    {
        friend class vspace;
        friend struct vmm::arch_table;

        private:
        static std::uintptr_t pa_mask;

        static const std::uintptr_t valid_table_flags;
        static const std::uintptr_t new_table_flags;

        class entry
        {
            private:
            std::uintptr_t _value = 0;

            class accessor
            {
                friend class entry;

                private:
                std::uintptr_t &_parent;

                accessor(std::uintptr_t &parent)
                    : _parent { parent }, value { _parent } { }

                public:
                std::uintptr_t value = 0;

                accessor &clear() { value = 0; return *this; }
                accessor &clearflags() { value &= pa_mask; return *this; }

                accessor &setflags(std::uintptr_t aflags, bool enabled)
                {
                    if (enabled)
                        value |= aflags;
                    else
                        value &= ~aflags;
                    return *this;
                }

                bool getflags(std::uintptr_t aflags) const
                {
                    return (value & aflags) == aflags;
                }

                std::uintptr_t getflags() const
                {
                    return value & ~pa_mask;
                }

                accessor &setaddr(std::uintptr_t paddr)
                {
                    value = (value & ~pa_mask) | (paddr & pa_mask);
                    return *this;
                }

                std::uintptr_t getaddr() const
                {
                    return value & pa_mask;
                }

                accessor &write() { _parent = value; return *this; }
            };

            public:
            accessor access() { return _value; }
        };

        struct [[gnu::packed]] table
        {
            entry entries[512];
        };

        table *_table;
        lib::spinlock_irq _lock;

        static table *new_table();
        static void free_table(table *ptr);

        static page_size fixpsize(page_size psize);
        static void invalidate(std::uintptr_t vaddr);

        static std::uintptr_t to_arch(pflag flags, caching cache, page_size psize);
        static auto from_arch(std::uintptr_t flags, page_size psize) -> std::pair<pflag, caching>;

        static auto getlvl(entry &entry, bool allocate) -> table *;

        auto getpte(std::uintptr_t vaddr, page_size psize, bool allocate) -> std::expected<std::reference_wrapper<entry>, error>;

        public:
        auto get_arch_table(std::uintptr_t addr = 0) const -> table *;

        [[gnu::pure]] static bool is_canonical(std::uintptr_t addr);

        [[gnu::pure]] static std::pair<std::uintptr_t, std::uintptr_t> user_range();
        [[gnu::pure]] static std::pair<std::uintptr_t, std::uintptr_t> kernel_range();

        [[gnu::pure]] static std::size_t from_page_size(page_size psize);
        [[gnu::pure]] static page_size max_page_size(std::size_t size);

        [[gnu::pure]] static inline page_size max_page_size(std::uintptr_t addr, std::size_t size)
        {
            auto ret = max_page_size(size);
            while (ret != page_size::small && addr % from_page_size(ret) != 0)
                ret = static_cast<page_size>(std::to_underlying(ret) - 1);
            return ret;
        }

        std::expected<void, error> map(std::uintptr_t vaddr, std::uintptr_t paddr, std::size_t length, pflag flags = pflag::rw, page_size psize = page_size::small, caching cache = caching::normal);
        std::expected<void, error> map_alloc(std::uintptr_t vaddr, std::size_t length, pflag flags = pflag::rw, page_size psize = page_size::small, caching cache = caching::normal);

        std::expected<void, error> protect(std::uintptr_t vaddr, std::size_t length, pflag flags = pflag::rw, page_size psize = page_size::small, caching cache = caching::normal);
        std::expected<void, error> unmap(std::uintptr_t vaddr, std::size_t length, page_size psize = page_size::small);
        std::expected<void, error> unmap_dealloc(std::uintptr_t vaddr, std::size_t length, page_size psize = page_size::small);

        std::expected<std::uintptr_t, error> translate(std::uintptr_t vaddr, page_size psize = page_size::small);

        void load() const;

        pagemap();
        pagemap(pagemap *ref) : _table { ref->_table } { }
        pagemap(table *ref) : _table { ref } { }

        ~pagemap();
    };

    inline frg::manual_box<pagemap> kernel_pagemap;
} // export namespace vmm