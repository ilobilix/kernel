// Copyright (C) 2024-2026  ilobilo

export module system.memory.virt;

export import :pagemap;
import system.memory.phys;
import lib;
import std;

export namespace vmm
{
    enum prot
    {
        none = 0x00,
        read = 0x01,
        write = 0x02,
        exec = 0x04
    };

    enum flag
    {
        failed = -1,
        file = 0x00,
        shared = 0x01,
        private_ = 0x02,
        fixed = 0x10,
        anonymous = 0x20,

        untouchable = 0x40
    };

    class object
    {
        protected:
        lib::locker<
            lib::btree::map<std::size_t, std::uintptr_t>,
            lib::mutex
        > pages;

        private:
        virtual std::uintptr_t request_page(std::size_t idx) = 0;
        virtual void write_back() = 0;

        public:
        object() = default;
        virtual ~object() { };

        std::uintptr_t get_page(std::size_t idx);

        std::size_t read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);
        std::size_t write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);
        std::size_t clear(std::uint64_t offset, std::uint8_t value, std::size_t length);

        std::size_t copy_to(object &other, std::uint64_t offset, std::size_t length);
    };

    class memobject : public object
    {
        private:
        std::uintptr_t request_page(std::size_t idx) override;
        void write_back() override;

        public:
        ~memobject();
    };

    struct mapping
    {
        std::uintptr_t startp;
        std::uintptr_t endp;

        std::shared_ptr<object> obj;
        off_t offsetp;

        std::uint8_t prot;
        std::uint8_t flags;

        lib::rbtree_hook<mapping> rbtree_hook;
        lib::interval_hook<std::uintptr_t> interval_hook;

        friend bool operator<(const mapping &lhs, const mapping &rhs)
        {
            return lhs.startp < rhs.startp;
        }
    };

    struct vmspace
    {
        std::shared_ptr<pagemap> pmap;
        lib::locker<
            lib::interval_tree_alloc<
                mapping,
                std::uintptr_t,
                &mapping::startp,
                &mapping::endp,
                &mapping::rbtree_hook,
                &mapping::interval_hook
            >,
            lib::rwmutex
        > tree;

        static constexpr std::uintptr_t mmap_min = 0x10000;
        static constexpr std::uintptr_t mmap_top = 0x7FFFF7000000;
        static constexpr std::uintptr_t stack_top = 0x7FFFFFFFF000;

        std::uintptr_t brk_start = 0;
        std::uintptr_t brk = 0;
        inline void init_brk(std::uintptr_t addr)
        {
            brk_start = brk = addr;
        }

        lib::expect<void> map(
            std::uintptr_t address, std::size_t length,
            std::uint8_t prot, std::uint8_t flags,
            std::shared_ptr<object> obj, off_t offset
        );
        lib::expect<void> unmap(std::uintptr_t address, std::size_t length);
        lib::expect<void> unmap(std::shared_ptr<object> obj);
        lib::expect<void> protect(std::uintptr_t address, std::size_t length,std::uint8_t prot);

        bool is_mapped(std::uintptr_t addr, std::size_t length);
        std::optional<std::uintptr_t> find_free_region(std::size_t length);

        ~vmspace() { lib::panic_if(pmap.use_count() != 1); }
    };

    std::size_t default_page_size();

    bool handle_pfault(std::uintptr_t addr, bool on_write);

    std::uintptr_t alloc_vspace(std::size_t pages);

    void init();
    void init_vspaces();
} // export namespace vmm







// TODO: WIP
export namespace vmm::uvm
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
        anonymous = 0x20
    };

    enum madv : madv_t
    {
    };

    struct page;

    struct anon
    {
        page *pg;
        lib::intrusive_ptr_hook hook;

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

        lib::expect<void> read_pages(
            std::uint64_t offp, std::span<page *> pages,
            std::size_t idx, madv_t advise, flag_t flags
        );

        lib::expect<void> write_back(std::uint64_t offp, std::size_t num_pages, flag_t flags);

        std::size_t read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);
        std::size_t write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);
        std::size_t clear(std::uint64_t offset, std::uint8_t value, std::size_t length);

        object() = default;
        virtual ~object() { }

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
        static constexpr std::uintptr_t mmap_min = 0x1000;
        static constexpr std::uintptr_t mmap_max = 0x7FFFFFFFF000;

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

        lib::expect<std::uintptr_t> map(
            std::uintptr_t hint, std::size_t length,
            prot_t prot, prot_t max_prot, flag_t flags,
            object::ptr obj, std::uint64_t offset
        );

        lib::expect<void> unmap(std::uintptr_t address, std::size_t length);
        lib::expect<void> unmap(object::ptr obj);

        lib::expect<void> protect(std::uintptr_t address, std::size_t length, prot_t prot);

        lib::expect<std::uintptr_t> find_free_region(std::size_t length);

        ~vmspace()
        {
            lib::panic_if(pmap.use_count() != 1);
            tree.write_lock()->clear([](entry *x) { delete x; });
        }
    };

    std::size_t default_page_size();

    page *page_for(std::uintptr_t addr);

    template<typename Type> requires (std::is_pointer_v<Type>)
    inline page *page_for(Type ptr)
    {
        return page_for(reinterpret_cast<std::uintptr_t>(ptr));
    }

    std::uintptr_t paddr_from(page *pg);

    bool handle_pfault(std::uintptr_t vaddr, bool on_write);
} // export namespace vmm::uvm

// TODO: TMP
export namespace vmm
{
    using page = uvm::page;
    auto page_for(auto addr) { return uvm::page_for(addr); }
} // export namespace vmm
