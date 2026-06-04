// Copyright (C) 2024-2026  ilobilo

export module system.memory.virt:pagemap;

import system.cpu.arch;
import magic_enum;
import frigg;
import lib;
import std;

namespace vmm
{
    struct arch_table;
} // namespace vmm

export namespace vmm
{
    inline constexpr std::size_t levels = 4;

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

    using magic_enum::bitwise_operators::operator~;
    using magic_enum::bitwise_operators::operator&;
    using magic_enum::bitwise_operators::operator&=;
    using magic_enum::bitwise_operators::operator|;
    using magic_enum::bitwise_operators::operator|=;

    enum class page_size
    {
        // do not modify
        small,
        medium,
        large
    };

    using asid_t = std::size_t;

    struct flush_range
    {
        std::uintptr_t start = ~0ul;
        std::uintptr_t end = 0;

        void extend(std::uintptr_t start, std::uintptr_t end)
        {
            if (start < this->start)
                this->start = start;
            if (end > this->end)
                this->end = end;
        }

        bool valid() const { return start < end; }
        std::size_t length() const { return end - start; }
    };

    struct asid_ctx
    {
        std::uint64_t gen;
        asid_t asid;
    };

    class pagemap
    {
        friend class vspace;
        friend struct vmm::arch_table;

        private:
        static constexpr std::size_t asid_mask = (1uz << cpu::tlb::asid_bits) - 1;
        static constexpr std::size_t max_asids = (1uz << cpu::tlb::asid_bits);

        static std::uintptr_t pa_mask;

        static const std::uintptr_t valid_table_flags;
        static const std::uintptr_t new_user_table_flags;
        static const std::uintptr_t new_kernel_table_flags;

        class entry
        {
            private:
            std::atomic<std::uintptr_t> _value = 0;

            class accessor
            {
                friend class entry;

                private:
                std::atomic<std::uintptr_t> &_parent;

                accessor(std::atomic<std::uintptr_t> &parent)
                    : _parent { parent }, value { _parent.load(std::memory_order_acquire) } { }

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

                bool is_large() const;

                accessor &write()
                {
                    _parent.store(value, std::memory_order_release);
                    return *this;
                }
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

        std::unique_ptr<std::atomic_uint64_t []> _asid_ctx;

        static table *new_table();
        static void free_table(table *ptr);

        static page_size fixpsize(page_size psize);

        static std::uintptr_t to_arch(pflag flags, caching cache, page_size psize);
        static auto from_arch(std::uintptr_t flags, page_size psize) -> std::pair<pflag, caching>;

        static asid_t alloc_asid();

        static auto getlvl(
            entry &entry, bool allocate, bool split,
            page_size psize, bool user
        ) -> table *;

        lib::expect<entry *> getpte(
            std::uintptr_t vaddr, page_size psize,
            bool allocate, bool split
        );

        lib::expect<void> map_internal(
            std::uintptr_t vaddr, std::uintptr_t paddr, std::size_t length, pflag flags,
            std::optional<page_size> psize, caching cache, flush_range &fr_out
        );
        lib::expect<void> protect_internal(
            std::uintptr_t vaddr, std::size_t length, pflag flags,
            std::optional<page_size> psize, caching cache, flush_range &fr_out
        );
        lib::expect<void> unmap_internal(
            std::uintptr_t vaddr, std::size_t length,
            std::optional<page_size> psize, flush_range &fr_out
        );

        void arch_load(asid_t asid, bool flush) const;

        public:
        auto get_arch_table(std::uintptr_t addr = 0) const -> table *;

        void invalidate(std::uintptr_t vaddr, std::size_t length);

        bool has_asid_ctx() const { return _asid_ctx != nullptr; }
        std::optional<asid_ctx> cached_asid_ctx(std::size_t cpu_idx) const
        {
            if (!_asid_ctx)
                return std::nullopt;

            const auto raw = _asid_ctx[cpu_idx].load(std::memory_order_acquire);
            if (raw == 0)
                return std::nullopt;

            return asid_ctx {
                .gen  = raw >> cpu::tlb::asid_bits,
                .asid = static_cast<asid_t>(raw & asid_mask),
            };
        }

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

        lib::expect<void> map(
            std::uintptr_t vaddr, std::uintptr_t paddr, std::size_t length, pflag flags = pflag::rw,
            std::optional<page_size> psize = std::nullopt, caching cache = caching::normal
        );
        lib::expect<void> protect(
            std::uintptr_t vaddr, std::size_t length, pflag flags = pflag::rw,
            std::optional<page_size> psize = std::nullopt, caching cache = caching::normal
        );
        lib::expect<void> unmap(
            std::uintptr_t vaddr, std::size_t length,
            std::optional<page_size> psize = std::nullopt
        );

        lib::expect<std::uintptr_t> translate(std::uintptr_t vaddr, page_size psize);

        void load() const;
        void unload() const;

        pagemap();
        pagemap(pagemap *ref) : _table { ref->_table } { }
        pagemap(table *ref) : _table { ref } { }

        ~pagemap();
    };

    inline frg::manual_box<pagemap> kernel_pagemap;
} // export namespace vmm
