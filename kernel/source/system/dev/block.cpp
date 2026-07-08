// Copyright (C) 2024-2026  ilobilo

module system.dev.block;

import system.sched.wait_queue;

namespace dev::block
{
    namespace
    {
        class block_class_t : public class_t
        {
            public:
            block_class_t() : dev::class_t { "block", dev::empty_ktype(), true } { }

            std::string devnode(const dev::device_t &dev, mode_t &mode) const override
            {
                lib::unused(mode);
                return dev.name;
            }
        };

        lib::initgraph::task register_task
        {
            "dev.block.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { core_registered_stage() },
            [] {
                lib::bug_on(!register_class(get_class()));
            }
        };
    } // namespace

    lib::expect<void> drive_t::rw(
        bool write, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
    )
    {
        if (buffer.size() == 0)
            return { };
        if (offset > size_bytes() || buffer.size() > size_bytes() - offset)
            return std::unexpected { lib::err::invalid_argument };

        std::size_t lba = offset >> _lba_shift;
        std::size_t nlb = ((offset + buffer.size() - 1) >> _lba_shift) - lba + 1;
        lib::bug_on(lba + nlb > _lba_count);

        struct chunk_t
        {
            arch::dma_buffer dma;
            std::span<std::byte> dma_span;
            lib::maybe_uspan<std::byte> window;
        };
        lib::list<chunk_t> chunks;

        std::atomic_size_t num = 0;
        std::atomic_bool failed = false;

        lib::err first_error;
        sched::wait_queue_t drain;

        std::size_t remaining = buffer.size();
        std::size_t progress = 0;

        auto misalign = offset - (lba << _lba_shift);
        while (nlb != 0)
        {
            const auto chunk = std::min(nlb, _max_transfer_lba);

            auto &st = chunks.emplace_back();
            st.dma = arch::dma_buffer { &_pool, chunk << _lba_shift };
            st.dma_span = std::span {
                st.dma.byte_data() + misalign,
                std::min(st.dma.size() - misalign, remaining)
            };
            st.window = buffer.subspan(progress, st.dma_span.size());

            if (write && !st.window.copy_to(st.dma_span))
            {
                failed.store(true, std::memory_order_release);
                first_error = lib::err::invalid_address;
                break;
            }

            num.fetch_add(1, std::memory_order_relaxed);

            rw(write, lba, st.dma, [&, pst = &st](lib::expect<void> res) {
                if (!res)
                {
                    if (!failed.exchange(true, std::memory_order_acq_rel))
                        first_error = res.error();
                }
                else if (!write && !pst->window.copy_from(pst->dma_span))
                {
                    if (!failed.exchange(true, std::memory_order_acq_rel))
                        first_error = lib::err::invalid_address;
                }

                if (num.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    drain.wake_all();
            });

            if (!write && !st.window.copy_from(st.dma_span))
                return std::unexpected { lib::err::invalid_address };

            lba += chunk;
            nlb -= chunk;
            progress += st.dma_span.size();
            remaining -= st.dma_span.size();

            misalign = 0;
        }

        while (num.load(std::memory_order_acquire) != 0)
        {
            const auto gen = drain.snapshot_gen();
            if (num.load(std::memory_order_acquire) == 0)
                break;
            drain.wait_unkillable_prepared(gen);
        }

        if (failed.load(std::memory_order_acquire))
            return std::unexpected { first_error };
        return { };
    }

    class_t &get_class()
    {
        static block_class_t blk { };
        return blk;
    }

    std::uint32_t alloc_major()
    {
        static std::uint32_t next = 254;
        lib::panic_if(next < 234, "could not allocate block device major");
        return next--;
    }
} // namespace dev::block
