// Copyright (C) 2024-2026  ilobilo

export module system.dev.block;

import system.dev;
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
            bool write, std::uint64_t lba, arch::dma_buffer &buffer,
            std::function<void (lib::expect<void>)> cb
        ) = 0;

        lib::expect<void> rw(bool write, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer);

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

        lib::expect<void> read(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        { return rw(false, offset, buffer); }

        lib::expect<void> write(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        { return rw(true, offset, buffer); }

        virtual lib::expect<void> flush() { return { }; };
    };

    class_t &get_class();

    std::uint32_t alloc_major();
} // export namespace dev::block
