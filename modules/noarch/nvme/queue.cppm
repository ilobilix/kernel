// Copyright (C) 2024-2026  ilobilo

export module nvme:queue;

import :cmd;

export namespace nvme
{
    class queue_t
    {
        private:
        std::vector<std::shared_ptr<command_t>> _cmds;
        sched::wait_queue_t _slot_free;
        lib::basic_bitmap<std::uint64_t> _cids;

        lib::spinlock_irq _lock;
        arch::mem_space _sq_db, _cq_db;

        std::uint32_t _depth;

        spec::completion_entry_t *_cqes;
        void *_sqcmds;

        std::uintptr_t _sq, _cq;
        std::uint16_t _sq_tail, _cq_head;

        bool _cq_phase;

        std::pair<std::size_t, std::size_t> get_sizes()
        {
            const std::size_t align = 0x1000;
            return {
                ((_depth << 6) + align - 1) & ~(align - 1),
                ((_depth * sizeof(spec::completion_entry_t)) + align - 1) & ~(align - 1)
            };
        }

        public:
        queue_t(std::uint16_t depth, arch::mem_space sq_db, arch::mem_space cq_db);
        ~queue_t();

        std::uintptr_t sq_paddr() const { return _sq; }
        std::uintptr_t cq_paddr() const { return _cq; }

        void process();
        void submit(const std::shared_ptr<command_t> &cmd);
    };
} // export namespace nvme
