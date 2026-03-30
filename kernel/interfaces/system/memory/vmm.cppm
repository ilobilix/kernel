// Copyright (C) 2024-2026  ilobilo

export module system.memory.virt;
export import :pagemap;

import system.memory.phys;
import lib;
import std;

export namespace vmm
{
    using prot_t = std::uint32_t;
    using flag_t = std::uint32_t;
    using madv_t = std::uint32_t;

    enum prot : prot_t
    {
        none = 0x00,
        read = 0x01,
        write = 0x02,
        exec = 0x04
    };

    enum flag : flag_t
    {
        file = 0x00,
        shared = 0x01,
        private_ = 0x02,
        fixed = 0x10,
        anonymous = 0x20,
        fixed_noreplace = 0x100000,

        // custom
        untouchable = 0x40
    };

    enum madv : madv_t
    {
    };

    struct page;

    struct anon
    {
        page *pg;
        lib::intrusive_ptr_hook hook;

        ~anon();

        using ptr = lib::intrusive_ptr<anon, &anon::hook>;
    };

    struct anon_map
    {
        lib::mutex lock;
        lib::intrusive_ptr_hook hook;

        // TODO: some kind of tree
        std::unique_ptr<anon::ptr []> slots;
        std::size_t nslots;

        using ptr = lib::intrusive_ptr<anon_map, &anon_map::hook>;
    };

    struct object;
    struct alignas(64) page
    {
        enum flag : flag_t
        {
            busy = (1 << 0),
            dirty = (1 << 1),
            file = (1 << 2),
            anonymous = (1 << 3)
        };

        std::atomic<flag_t> flags;
        std::atomic<std::uint32_t> refcount;

        void ref()
        {
            refcount.fetch_add(1, std::memory_order_relaxed);
        }

        bool unref(std::size_t num = 1)
        {
            return refcount.fetch_sub(num, std::memory_order_acq_rel) == num;
        }

        struct {
            std::uint64_t next_paddr : pmm::paddr_bits - pmm::page_bits;
            std::uint64_t order : std::bit_width(pmm::max_order);
            std::uint64_t allocated : 1;
        } buddy;

        union {
            struct {
                object *obj_ptr;
                std::uint64_t offp;
            };

            struct {
                anon *anon_ptr;
            };
        };
    };
    static_assert(sizeof(page) == 64);

    struct object
    {
        static constexpr std::size_t max_readahead = 32;
        static_assert(!(max_readahead & 0x07));

        private:
        lib::locker<
            lib::btree::map<std::size_t, page *>,
            lib::mutex
        > cache;

        protected:
        virtual lib::expect<void> fetch_pages(std::size_t idx, std::span<page *> pages) = 0;
        virtual lib::expect<void> write_pages(std::size_t idx, std::span<page *> pages) = 0;

        std::size_t apply_func(std::uint64_t offset, std::size_t size, auto func);

        public:
        lib::intrusive_ptr_hook hook;

        lib::expect<void> read_pages(std::uint64_t offp, std::span<page *> pages, std::size_t idx);
        lib::expect<void> write_back(std::uint64_t offp, std::size_t num_pages);

        std::size_t read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);
        std::size_t write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);
        std::size_t clear(std::uint64_t offset, std::uint8_t value, std::size_t length);

        object() = default;
        virtual ~object();

        using ptr = lib::intrusive_ptr<object, &object::hook>;
    };

    struct memobject : object
    {
        private:
        lib::expect<void> fetch_pages(std::size_t idx, std::span<page *> pages) override
        {
            lib::unused(idx, pages);
            return { };
        }

        lib::expect<void> write_pages(std::size_t idx, std::span<page *> pages) override
        {
            lib::unused(idx, pages);
            return { };
        }
    };

    struct entry
    {
        std::uintptr_t startp;
        std::uintptr_t endp;

        object::ptr obj;
        std::uint64_t offp;

        anon_map::ptr amap;
        std::uint64_t anon_idx;

        prot_t prot;
        prot_t max_prot;
        flag_t flags;

        lib::rbtree_hook<entry> hook;
        lib::interval_hook<std::uintptr_t> interval;
    };

    struct vmspace
    {
        private:
        lib::expect<std::uintptr_t> find_free_region_internal(auto &locked, std::size_t length);

        public:
        static constexpr std::uintptr_t mmap_min = 0x10000;
        static constexpr std::uintptr_t mmap_max = 0x7FFFF7000000;
        static constexpr std::uintptr_t vspace_top = 0x7FFFFFFFF000; // TODO: arch?

        std::shared_ptr<pagemap> pmap;
        lib::locker<
            lib::interval_tree<
                entry,
                std::uintptr_t,
                &entry::startp,
                &entry::endp,
                &entry::hook,
                &entry::interval
            >,
            lib::rwmutex
        > tree;

        std::uintptr_t current_brk;

        lib::expect<std::uintptr_t> map(
            std::uintptr_t hint, std::size_t length,
            prot_t prot, prot_t max_prot, flag_t flags,
            object::ptr obj, std::uint64_t offset
        );
        lib::expect<void> unmap(std::uintptr_t address, std::size_t length);
        lib::expect<void> protect(std::uintptr_t address, std::size_t length, prot_t prot);

        lib::expect<std::uintptr_t> find_free_region(std::size_t length);

        ~vmspace()
        {
            lib::panic_if(pmap.use_count() != 1);
            tree.write_lock()->clear([](entry *x) {
                // object and anon free their pages
                delete x;
            });
        }
    };

    constexpr page_size default_page_size()
    {
        return page_size::small;
    }

    page *page_for(std::uintptr_t addr);
    std::uintptr_t paddr_from(page *pg);

    template<typename Type>
        requires (std::is_pointer_v<Type>)
    inline page *page_for(Type ptr)
    {
        return page_for(reinterpret_cast<std::uintptr_t>(ptr));
    }

    struct pfault_state
    {
        std::uintptr_t address;
        bool is_present;
        bool is_write;
        bool is_exec;
        bool is_user;
    };

    bool handle_pfault(pfault_state state);

    std::uintptr_t alloc_vspace(std::size_t length);

    void init();
    void init_vspaces();
} // export namespace vmm
