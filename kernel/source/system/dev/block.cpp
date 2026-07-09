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
        bool write, bool sync, std::uint64_t offset, std::size_t total_size,
        std::function_ref<lib::maybe_uspan<std::byte> (std::size_t idx)> getter
    )
    {
        lib::bug_on(!write && !sync);

        if (total_size == 0)
            return { };
        if (offset > size_bytes() || total_size > size_bytes() - offset)
            return std::unexpected { lib::err::invalid_argument };

        std::size_t lba = offset >> _lba_shift;
        std::size_t nlb = ((offset + total_size - 1) >> _lba_shift) - lba + 1;
        lib::bug_on(lba + nlb > _lba_count);

        struct chunk_t
        {
            arch::dma_buffer dma;
            std::span<std::byte> dma_span;
            std::size_t win_idx;
            std::size_t win_off;
            std::atomic_bool done = false;
        };
        lib::list<chunk_t> chunks;

        auto do_copy = [&getter](bool to_dma, std::size_t idx, std::size_t off,
            std::span<std::byte> dma_span)
        {
            std::size_t pos = 0;
            const auto total = dma_span.size();

            while (pos != total)
            {
                auto src = getter(idx);
                const auto available = src.size() - off;
                const auto take = std::min(available, total - pos);

                auto seg = src.subspan(off, take);
                auto dst = dma_span.subspan(pos, take);
                if (!(to_dma ? seg.copy_to(dst) : seg.copy_from(dst)))
                    return false;

                pos += take;
                off += take;

                if (off == src.size())
                {
                    idx++;
                    off = 0;
                }
            }
            return true;
        };

        std::atomic_size_t num = 0;
        std::atomic_bool failed = false;

        lib::err first_error;
        sched::wait_queue_t drain;

        std::size_t remaining = total_size;
        auto misalign = offset - (lba << _lba_shift);

        std::size_t buf_idx = 0;
        std::size_t buf_off = 0;

        while (nlb != 0)
        {
            const auto chunk = std::min(nlb, _max_transfer_lba);

            auto &st = chunks.emplace_back();
            st.dma = arch::dma_buffer { &_pool, chunk << _lba_shift };
            st.dma_span = std::span {
                st.dma.byte_data() + misalign,
                std::min(st.dma.size() - misalign, remaining)
            };

            st.win_idx = buf_idx;
            st.win_off = buf_off;

            auto need = st.dma_span.size();
            while (need != 0)
            {
                auto src = getter(buf_idx);
                const auto avail = src.size() - buf_off;
                const auto take = std::min(avail, need);

                need -= take;
                buf_off += take;

                if (buf_off == src.size())
                {
                    buf_idx++;
                    buf_off = 0;
                }
            }

            if (write && !do_copy(true, st.win_idx, st.win_off, st.dma_span))
            {
                failed.store(true, std::memory_order_release);
                first_error = lib::err::invalid_address;
                break;
            }

            num.fetch_add(1, std::memory_order_relaxed);

            rw(write, sync, lba, st.dma, [&, pst = &st](lib::expect<void> res) {
                if (!sync)
                    return;

                if (!res)
                {
                    if (!failed.exchange(true, std::memory_order_acq_rel))
                        first_error = res.error();
                }

                pst->done.store(true, std::memory_order_release);
                num.fetch_sub(1, std::memory_order_acq_rel);
                drain.wake_all();
            });

            lba += chunk;
            nlb -= chunk;
            remaining -= st.dma_span.size();
            misalign = 0;
        }

        if (sync)
        {
            if (!write)
            {
                for (const auto &st : chunks)
                {
                    while (!st.done.load(std::memory_order_acquire))
                    {
                        const auto gen = drain.snapshot_gen();
                        if (st.done.load(std::memory_order_acquire))
                            break;
                        drain.wait_unkillable_prepared(gen);
                    }

                    if (!failed.load(std::memory_order_acquire) &&
                        !do_copy(false, st.win_idx, st.win_off, st.dma_span))
                    {
                        if (!failed.exchange(true, std::memory_order_acq_rel))
                            first_error = lib::err::invalid_address;
                    }
                }
            }
            else
            {
                while (num.load(std::memory_order_acquire) != 0)
                {
                    const auto gen = drain.snapshot_gen();
                    if (num.load(std::memory_order_acquire) == 0)
                        break;
                    drain.wait_unkillable_prepared(gen);
                }
            }

            if (failed.load(std::memory_order_acquire))
                return std::unexpected { first_error };
        }

        return { };
    }

    lib::expect<void> object_t::fetch_pages(std::size_t idx, std::span<vmm::page *> pages)
    {
        auto drv = drive.lock();
        if (!drv)
            return std::unexpected { lib::err::invalid_device_or_address };

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);

        auto range = pages | std::views::transform([npsize](vmm::page *pg) {
            return lib::maybe_uspan<std::byte>::create(
                reinterpret_cast<std::byte *>(lib::tohh(vmm::paddr_from(pg))),
                npsize
            ).value();
        });

        return drv->rw(false, true, idx * npsize, range);
    }

    lib::expect<void> object_t::write_pages(std::size_t idx, std::span<vmm::page *> pages)
    {
        auto drv = drive.lock();
        if (!drv)
            return std::unexpected { lib::err::invalid_device_or_address };

        const auto psize = vmm::default_page_size();
        const auto npsize = vmm::pagemap::from_page_size(psize);

        auto range = pages | std::views::transform([npsize](vmm::page *pg) {
            return lib::maybe_uspan<std::byte>::create(
                reinterpret_cast<std::byte *>(lib::tohh(vmm::paddr_from(pg))),
                npsize
            ).value();
        });

        return drv->rw(true, true, idx * npsize, range);
    }

    lib::expect<std::size_t> ops_t::read(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        lib::unused(file);

        auto drv = get_memory().drive.lock();
        if (!drv)
            return std::unexpected { lib::err::invalid_device_or_address };

        if (offset >= drv->size_bytes())
            return 0;

        auto real_size = buffer.size();
        if (buffer.size() > drv->size_bytes() - offset)
            real_size = drv->size_bytes() - offset;

        if (const auto ret = drv->rw(false, true, offset, std::views::single(buffer)); !ret)
            return std::unexpected { ret.error() };
        return real_size;
    }

    lib::expect<std::size_t> ops_t::write(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto drv = get_memory().drive.lock();
        if (!drv)
            return std::unexpected { lib::err::invalid_device_or_address };

        if (offset >= drv->size_bytes())
            return std::unexpected { lib::err::no_space_left };
        auto real_size = buffer.size();
        if (buffer.size() > drv->size_bytes() - offset)
            real_size = drv->size_bytes() - offset;

        const bool sync = (file->flags & vfs::o_sync) != 0;
        if (const auto ret = drv->rw(true, sync, offset, std::views::single(buffer)); !ret)
            return std::unexpected { ret.error() };
        return real_size;
    }

    lib::expect<vmm::object::ptr> ops_t::map(std::shared_ptr<vfs::file> file)
    {
        lib::unused(file);
        return memory;
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
