// Copyright (C) 2024-2026  ilobilo

export module nvme:ns;

import system.dev.block;

import :cmd;
import :queue;

export namespace nvme
{
    class namespace_t : public dev::block::drive_t
    {
        private:
        std::uint32_t _nsid;

        std::span<std::unique_ptr<queue_t>> _io_queues;
        bool _vwc;

        void submit(const std::shared_ptr<command_t> &cmd);

        dev_t alloc_id() override;

        void rw(
            bool write, bool sync, std::uint64_t lba, arch::dma_buffer &buffer,
            std::function<void (lib::expect<void>)> cb
        ) override;

        public:
        namespace_t(
            std::uint32_t nsid, std::uint8_t lba_shift, std::uint64_t lba_count,
            arch::dma_pool &pool, std::span<std::unique_ptr<queue_t>> io_queues,
            std::size_t max_transfer_lba, bool vwc
        ) : drive_t { lba_shift, lba_count, std::min(max_transfer_lba, 0x10000zu), pool },
            _nsid { nsid }, _io_queues { io_queues }, _vwc { vwc } { }

        std::uint32_t nsid() const { return _nsid; }

        lib::expect<void> flush() override;
    };
} // export namespace nvme
