// Copyright (C) 2024-2026  ilobilo

module nvme;

import system.cpu.local;
import system.vfs.dev;

namespace nvme
{
    void namespace_t::submit(const std::shared_ptr<command_t> &cmd)
    {
        const auto idx = cpu::self().read<std::size_t, &cpu::processor::idx>();
        _io_queues[idx % _io_queues.size()]->submit(cmd);
    }

    dev_t namespace_t::alloc_id()
    {
        return vfs::dev::makedev(vfs::dev::major(dev->devt), dev::block::alloc_minor());
    }

    void namespace_t::rw(
        bool write, bool sync, std::uint64_t lba, arch::dma_buffer &buffer,
        std::function<void (lib::expect<void>)> cb
    )
    {
        auto cmd = std::make_shared<command_t>(_pool);
        cmd->setup(buffer);

        auto &buf = cmd->buffer().rw;
        buf.opcode = write ? spec::write : spec::read;
        buf.nsid = _nsid;
        buf.start_lba = lba;
        buf.length = (buffer.size() >> _lba_shift) - 1;
        buf.control = (write && sync) ? 0x4000 : 0;

        cmd->on_complete([cb = std::move(cb)](command_t::result res) {
            cb(res.first.successful()
                ? lib::expect<void> { }
                : std::unexpected { lib::err::io_error }
            );
        });
        submit(cmd);
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
