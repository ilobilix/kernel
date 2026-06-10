// Copyright (C) 2024-2026  ilobilo

module nvme;

import system.memory.phys;

namespace nvme
{
    queue_t::queue_t(std::uint16_t depth, arch::mem_space sq_db, arch::mem_space cq_db)
        : _cids { static_cast<std::uint16_t>(depth - 1) }, _sq_db { sq_db }, _cq_db { cq_db },
          _depth { depth }, _sq_tail { 0 }, _cq_head { 0 }, _cq_phase { true }
    {
        _cmds.resize(depth);

        const auto [sqsize, cqsize] = get_sizes();
        _sq = pmm::alloc(lib::div_roundup(sqsize, pmm::page_size), true);
        _cq = pmm::alloc(lib::div_roundup(cqsize, pmm::page_size), true);

        _sqcmds = reinterpret_cast<void *>(lib::tohh(_sq));
        _cqes = reinterpret_cast<spec::completion_entry_t *>(lib::tohh(_cq));
    }

    queue_t::~queue_t()
    {
        const auto [sqsize, cqsize] = get_sizes();
        pmm::free(_sq, lib::div_roundup(sqsize, pmm::page_size));
        pmm::free(_cq, lib::div_roundup(cqsize, pmm::page_size));
    }

    void queue_t::process()
    {
        bool freed = false;
        auto cqe = &_cqes[_cq_head];
        while ((cqe->status.status & 1) == _cq_phase)
        {
            std::atomic_thread_fence(std::memory_order_acquire);

            const auto slot = cqe->command_id;

            lib::bug_on(slot >= _cmds.size());
            lib::bug_on(!_cmds[slot]);

            std::move(_cmds[slot])->complete(*cqe);

            _cids.free(slot);
            freed = true;

            if (++_cq_head == _depth)
            {
                _cq_head = 0;
                _cq_phase ^= 1;
            }

            cqe = &_cqes[_cq_head];
        }

        if (freed)
        {
            _cq_db.store(arch::scalar_register<std::uint32_t> { 0 }, _cq_head);
            _slot_free.wake_all();
        }
    }

    void queue_t::submit(const std::shared_ptr<command_t> &cmd)
    {
        std::uint16_t slot = 0;
        while (true)
        {
            const auto gen = _slot_free.snapshot_gen();
            if (const auto res = _cids.alloc())
            {
                slot = *res;
                break;
            }
            _slot_free.wait_prepared(gen);
        }

        cmd->buffer().common.command_id = slot;
        _cmds[slot] = cmd;

        const std::unique_lock _ { _lock };

        std::memcpy(
            reinterpret_cast<std::byte *>(_sqcmds) + (_sq_tail << 6),
            &cmd->buffer(),
            sizeof(spec::command_t)
        );

        if (++_sq_tail == _depth)
            _sq_tail = 0;

        std::atomic_thread_fence(std::memory_order_release);
        _sq_db.store(arch::scalar_register<std::uint32_t> { 0 }, _sq_tail);
    }
} // namespace nvme
