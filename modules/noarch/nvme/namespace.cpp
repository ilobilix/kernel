// Copyright (C) 2024-2026  ilobilo

module nvme;

import system.cpu.local;

namespace nvme
{
    void namespace_t::submit(const std::shared_ptr<command_t> &cmd)
    {
        const auto idx = cpu::self().read<std::size_t, &cpu::processor::idx>();
        _io_queues[idx % _io_queues.size()]->submit(cmd);
    }

    lib::expect<void> namespace_t::rw(
        bool write, std::uint64_t lba, std::uint32_t nlb,
        lib::maybe_uspan<std::byte> buf
    )
    {
        if (nlb == 0)
            return { };
        if (lba + nlb > _lba_count)
            return std::unexpected { lib::err::invalid_argument };
        if (buf.size_bytes() < (static_cast<std::size_t>(nlb) << _lba_shift))
            return std::unexpected { lib::err::invalid_argument };

        const auto max_blocks = std::min(_max_transfer >> _lba_shift, 0x10000zu);

        std::size_t offset = 0;
        while (nlb != 0)
        {
            const std::uint32_t chunk = std::min<std::size_t>(nlb, max_blocks);
            const auto bytes = static_cast<std::size_t>(chunk) << _lba_shift;

            arch::dma_buffer dma { &_pool, bytes };
            const std::span dma_span { dma.byte_data(), bytes };
            const auto window = buf.subspan(offset, bytes);

            if (write && !window.copy_to(dma_span))
                return std::unexpected { lib::err::invalid_address };

            auto cmd = std::make_shared<command_t>(_pool);
            cmd->setup(dma);
            cmd->own(std::move(dma));

            auto &buf = cmd->buffer().rw;
            buf.opcode = write ? spec::write : spec::read;
            buf.nsid = _nsid;
            buf.start_lba = lba;
            buf.length = static_cast<std::uint16_t>(chunk - 1);

            submit(cmd);
            if (!cmd->wait(io_timeout_ms * 1'000'000ul) || !cmd->get().first.successful())
                return std::unexpected { lib::err::io_error };

            if (!write && !window.copy_from(dma_span))
                return std::unexpected { lib::err::invalid_address };

            lba += chunk;
            nlb -= chunk;
            offset += bytes;
        }
        return { };
    }

    lib::expect<void> namespace_t::flush()
    {
        if (!_vwc)
            return { };

        auto cmd = std::make_shared<command_t>(_pool);
        auto &buf = cmd->buffer().common;
        buf.opcode = spec::flush;
        buf.namespace_id = _nsid;

        submit(cmd);
        if (!cmd->wait(io_timeout_ms * 1'000'000ul) || !cmd->get().first.successful())
            return std::unexpected { lib::err::io_error };
        return { };
    }
} // namespace nvme
