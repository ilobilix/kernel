// Copyright (C) 2024-2026  ilobilo

export module nvme:queue;

import :cmd;

export namespace nvme
{
    template<std::size_t MaxIds>
    class cid_alloc_t
    {
        private:
        static constexpr std::size_t word_bits = 64;
        static constexpr std::size_t words = (MaxIds + word_bits - 1) / word_bits;

        std::array<std::atomic<std::uint64_t>, words> _free;
        std::size_t _words;

        public:
        cid_alloc_t(std::uint16_t capacity)
        {
            lib::bug_on(capacity > MaxIds);
            _words = (capacity + word_bits - 1) / word_bits;

            for (std::size_t i = 0; i < words; i++)
            {
                std::uint64_t mask = 0;
                if (const auto base = i * word_bits; base < capacity)
                {
                    const auto num = std::min(capacity - base, word_bits);
                    mask = num == word_bits ? ~0ul : (1ul << num) - 1;
                }
                _free[i].store(mask, std::memory_order_relaxed);
            }
        }

        std::optional<std::uint16_t> alloc()
        {
            for (std::size_t i = 0; i < _words; i++)
            {
                auto current = _free[i].load(std::memory_order_relaxed);
                while (current)
                {
                    const auto bit = std::countr_zero(current);
                    if (_free[i].compare_exchange_weak(current, current & (current - 1),
                        std::memory_order_acquire, std::memory_order_relaxed))
                        return i * word_bits + bit;
                }
            }
            return std::nullopt;
        }

        void free(std::uint16_t id)
        {
            const auto bit = 1ul << (id % word_bits);
            const auto prev = _free[id / word_bits].fetch_or(bit, std::memory_order_release);
            lib::bug_on(prev & bit);
        }
    };

    class queue_t
    {
        private:
        std::vector<std::shared_ptr<command_t>> _cmds;
        sched::wait_queue_t _slot_free;
        cid_alloc_t<max_queue_depth> _cids;

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
