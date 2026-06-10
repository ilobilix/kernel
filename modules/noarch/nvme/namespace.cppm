// Copyright (C) 2024-2026  ilobilo

export module nvme:ns;

import libarch;
import lib;
import std;

import :cmd;
import :queue;

export namespace nvme
{
    class namespace_t
    {
        private:
        std::uint32_t _nsid;
        std::uint8_t _lba_shift;
        std::uint64_t _lba_count;

        arch::dma_pool &_pool;
        std::span<std::unique_ptr<queue_t>> _io_queues;
        std::size_t _max_transfer;
        bool _vwc;

        void submit(const std::shared_ptr<command_t> &cmd);
        lib::expect<void> rw(
            bool write, std::uint64_t lba, std::uint32_t nlb,
            lib::maybe_uspan<std::byte> buf
        );

        public:
        namespace_t(
            std::uint32_t nsid, std::uint8_t lba_shift, std::uint64_t lba_count,
            arch::dma_pool &pool, std::span<std::unique_ptr<queue_t>> io_queues,
            std::size_t max_transfer, bool vwc
        ) : _nsid { nsid }, _lba_shift { lba_shift }, _lba_count { lba_count },
            _pool { pool }, _io_queues { io_queues }, _max_transfer { max_transfer },
            _vwc { vwc } { }

        std::uint32_t nsid() const { return _nsid; }
        std::uint32_t block_size() const { return 1u << _lba_shift; }
        std::uint64_t block_count() const { return _lba_count; }
        std::uint64_t size_bytes() const { return _lba_count << _lba_shift; }

        lib::expect<void> read(
            std::uint64_t lba, std::uint32_t nlb, lib::maybe_uspan<std::byte> buf
        )
        { return rw(false, lba, nlb, buf); }

        lib::expect<void> write(
            std::uint64_t lba, std::uint32_t nlb, lib::maybe_uspan<std::byte> buf
        )
        { return rw(true, lba, nlb, buf); }

        lib::expect<void> flush();
    };
} // export namespace nvme
