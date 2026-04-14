// Copyright (C) 2024-2025  ilobilo

export module system.memory.virt;
export import :pagemap;

import magic_enum;
import cppstd;
import lib;

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
        anonymous = 0x20
    };

    class object
    {
        protected:
        lib::locker<
            lib::btree::map<std::size_t, std::uintptr_t>,
            lib::mutex
        > pages;

        private:
        virtual std::uintptr_t get_page_internal(std::size_t idx) = 0;
        // TODO: sync

        public:
        object() = default;
        virtual ~object() { };

        std::uintptr_t get_page(std::size_t idx);
        std::size_t read(std::uint64_t offset, std::span<std::byte> buffer);
        std::size_t write(std::uint64_t offset, std::span<std::byte> buffer);

    };

    class physobject : public object
    {
        private:
        std::uintptr_t get_page_internal(std::size_t idx) override;

        public:
        ~physobject();
    };

    struct mapping
    {
        std::uintptr_t startp;
        std::uintptr_t endp;

        std::shared_ptr<object> obj;
        off_t offset;

        std::uint8_t prot;
        std::uint8_t flags;

        friend bool operator<(const mapping &lhs, const mapping &rhs)
        {
            return lhs.startp < rhs.startp;
        }
    };

    struct vmspace
    {
        std::shared_ptr<pagemap> pmap;
        lib::locker<
            lib::btree::multiset<mapping>,
            lib::rwmutex
        > tree;

        std::expected<void, error> map(
            std::uintptr_t address, std::size_t length,
            std::uint8_t prot, std::uint8_t flags,
            std::shared_ptr<object> obj, off_t offset
        );

        bool is_mapped(std::uintptr_t addr, std::size_t length);
    };

    bool handle_pfault(std::uintptr_t addr, bool on_write);

    using magic_enum::bitwise_operators::operator~;
    using magic_enum::bitwise_operators::operator&;
    using magic_enum::bitwise_operators::operator&=;
    using magic_enum::bitwise_operators::operator|;
    using magic_enum::bitwise_operators::operator|=;

    enum class space_type : std::size_t
    {
        stack,
        modules,
        acpi, pci,
        other
    };
    std::uintptr_t alloc_vpages(space_type type, std::size_t pages = 1);

    void init();
    void init_vspaces();
} // export namespace vmm