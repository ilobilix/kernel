// Copyright (C) 2024-2026  ilobilo

export module system.dev.block;

import system.memory.virt;
import system.dev;
import system.vfs;
import libarch;
import lib;
import std;

export namespace dev::block
{
    class drive_t
    {
        protected:
        std::uint8_t _lba_shift;
        std::uint64_t _lba_count;
        std::uint64_t _max_transfer_lba;

        arch::dma_pool &_pool;

        virtual void rw(
            bool write, bool sync, std::uint64_t lba, arch::dma_buffer &buffer,
            std::function<void (lib::expect<void>)> cb
        ) = 0;

        lib::expect<void> rw(
            bool write, bool sync, std::uint64_t offset, std::size_t total_size,
            std::function_ref<lib::maybe_uspan<std::byte> (std::size_t)> getter
        );

        public:
        drive_t(
            std::uint8_t lba_shift, std::uint64_t lba_count,
            std::uint64_t max_transfer_lba, arch::dma_pool &pool
        ) : _lba_shift { lba_shift }, _lba_count { lba_count },
            _max_transfer_lba { max_transfer_lba }, _pool { pool } { }

        virtual ~drive_t() = default;

        std::uint32_t block_size() const { return 1u << _lba_shift; }
        std::uint64_t block_count() const { return _lba_count; }
        std::uint64_t size_bytes() const { return _lba_count << _lba_shift; }
        std::uint64_t max_transfer_blocks() const { return _max_transfer_lba; }

        template<std::ranges::random_access_range Range>
            requires std::same_as<std::ranges::range_value_t<Range>, lib::maybe_uspan<std::byte>>
        lib::expect<void> rw(bool write, bool sync, std::uint64_t offset, Range &&range)
        {
            std::size_t total_size = 0;
            for (const auto &uspan : range)
                total_size += uspan.size();
            return rw(write, sync, offset, total_size, [&](std::size_t idx) { return range[idx]; });
        }

        virtual lib::expect<void> flush() { return { }; };
    };

    struct object_t : vmm::object
    {
        friend struct ops_t;

        private:
        std::weak_ptr<drive_t> drive;

        lib::expect<void> fetch_pages(std::size_t idx, std::span<vmm::page *> pages) override;
        lib::expect<void> write_pages(std::size_t idx, std::span<vmm::page *> pages) override;

        public:
        object_t(std::weak_ptr<drive_t> drive)
            : vmm::object { vmm::object_type::file }, drive { drive } { }
    };

    struct ops_t : vfs::ops
    {
        private:
        object_t::ptr memory;

        object_t &get_memory() { return static_cast<object_t &>(*memory); }

        public:
        ops_t(std::weak_ptr<drive_t> drive)
            : vfs::ops { }, memory { new object_t { std::move(drive) } } { }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file> file) override;
    };

    class_t &get_class();

    std::uint32_t alloc_major();
} // export namespace dev::block
