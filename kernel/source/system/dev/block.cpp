// Copyright (C) 2024-2026  ilobilo

module system.dev.block;

import system.sched.wait_queue;
import system.vfs.dev;
import fmt;

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

        // TODO
        struct part_ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t> attributes() const override
            {
                return { };
            }

            std::span<dev::bin_attribute_t> bin_attributes() const override
            {
                return { };
            }

            void fill_uevent(dev::kobject_t &kobj, dev::uevent_t &uev) override
            {
                lib::unused(kobj, uev);
            }
        };

        dev::ktype_t &get_part_ktype()
        {
            static part_ktype_t type { };
            return type;
        }

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
        std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto &mem = get_memory();
        auto drv = mem.drive.lock();
        if (!drv)
            return std::unexpected { lib::err::invalid_device_or_address };

        const auto real_block_size = mem.lba_count * drv->block_size();
        if (offset >= real_block_size)
            return 0;

        const auto real_size = std::min(buffer.size(), real_block_size - offset);
        offset += mem.lba_start * drv->block_size();

        if (file->flags & vfs::o_direct)
        {
            if (const auto ret = drv->rw(false, true, offset,
                    std::views::single(buffer.subspan(0, real_size))); !ret)
                return std::unexpected { ret.error() };
        }
        else mem.read(offset, buffer);

        return real_size;
    }

    lib::expect<std::size_t> ops_t::write(
        std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto &mem = get_memory();
        auto drv = mem.drive.lock();
        if (!drv)
            return std::unexpected { lib::err::invalid_device_or_address };

        const auto real_block_size = mem.lba_count * drv->block_size();
        if (offset >= real_block_size)
            return std::unexpected { lib::err::no_space_left };

        const auto real_size = std::min(buffer.size(), real_block_size - offset);
        offset += mem.lba_start * drv->block_size();

        const bool sync = (file->flags & vfs::o_sync) != 0;
        if (file->flags & vfs::o_direct)
        {
            if (const auto ret = drv->rw(true, sync, offset,
                    std::views::single(buffer.subspan(0, real_size))); !ret)
                return std::unexpected { ret.error() };
        }
        else mem.write(offset, buffer); // TODO: sync

        return real_size;
    }

    lib::expect<vmm::object::ptr> ops_t::map(std::shared_ptr<vfs::file_t> file)
    {
        lib::unused(file);
        return memory;
    }

    lib::expect<void> register_drive(std::shared_ptr<drive_t> drive, std::string_view part_prefix)
    {
        if (const auto ret = dev::register_device(drive->dev); !ret)
            return ret;

        const auto read_lbas = [&](std::uint64_t idx, std::uint32_t count)
            -> lib::expect<lib::membuffer>
        {
            lib::membuffer lba0 { count * 512 };
            auto uspan = lba0.uspan();
            lib::bug_on(!uspan);

            if (const auto ret = drive->rw(false, true, idx * 512, std::views::single(*uspan)); !ret)
                return std::unexpected { ret.error() };

            return lba0;
        };

        const auto lba0 = read_lbas(0, 1);
        if (!lba0.has_value())
            return std::unexpected { lba0.error() };

        const auto *mbr = reinterpret_cast<mbr::table_t *>(lba0->data());
        if (!mbr->is_valid())
            return { };

        const auto add_part = [&](std::uint64_t lba_start, std::uint64_t lba_count)
        {
            const auto max = drive->block_count();
            lib::bug_on(lba_start >= max || lba_count > max || lba_start > max - lba_count);

            auto part = dev::device_t::create(
                fmt::format("{}{}{}", drive->dev->name, part_prefix, drive->_parts.size() + 1),
                get_part_ktype(), drive->dev
            );
            part->cls = &get_class();
            part->devt = drive->alloc_id();
            part->fops = std::make_shared<ops_t>(drive, lba_start, lba_count);

            drive->_parts.push_back(part);

            if (const auto ret = dev::register_device(std::move(part)); !ret)
            {
                lib::error(
                    "block: could not register partition: {}",
                    lib::error_name(ret.error())
                );
            }
        };

        if (mbr->partitions[0].is_pmbr())
        {
            const auto read_header = [&](std::uint64_t lba) -> lib::expect<lib::membuffer>
            {
                auto buf = read_lbas(lba, 1);
                if (!buf.has_value())
                    return buf;

                auto hdr = reinterpret_cast<gpt::table_t *>(buf->data());
                if (!hdr->is_valid())
                    return std::unexpected { lib::err::corrupted_data };
                return buf;
            };

            const auto read_entries = [&](const gpt::table_t *hdr) -> lib::expect<lib::membuffer>
            {
                const auto bytes = hdr->parts * hdr->partentrysize;
                auto buf = read_lbas(hdr->guidpartlba, lib::div_roundup(bytes, 512));
                if (!buf.has_value())
                    return std::unexpected { buf.error() };

                if (lib::crc32::compute(buf->span().subspan(0, bytes)) != hdr->partchecksum)
                    return std::unexpected { lib::err::corrupted_data };
                return buf;
            };

            auto hdr_buf = read_header(1);
            bool backup = false;

            if (!hdr_buf)
            {
                lib::warn("block: invalid primary gpt header, trying backup");
                if (!(hdr_buf = read_header(drive->block_count() - 1)))
                {
                    lib::error("block: invalid backup gpt header");
                    return std::unexpected { lib::err::corrupted_data };
                }
                backup = true;
            }

            auto hdr = reinterpret_cast<gpt::table_t *>(hdr_buf->data());
            auto entries_buf = read_entries(hdr);
            if (!entries_buf)
            {
                lib::warn("block: invalid gpt partition entries checksum");

                if (!backup)
                {
                    if ((hdr_buf = read_header(drive->block_count() - 1)))
                    {
                        hdr = reinterpret_cast<gpt::table_t *>(hdr_buf->data());
                        if ((entries_buf = read_entries(hdr)))
                            lib::info("block: recovered partition entries from backup gpt");
                    }
                }

                if (!entries_buf)
                {
                    lib::error("block: no valid gpt partition entries found (primary or backup)");
                    return std::unexpected { lib::err::corrupted_data };
                }
            }

            lib::info("block: found gpt partition table");

            const auto ascii_name = [](std::u16string_view name) {
                std::string out;
                out.reserve(name.size());

                for (const auto chr : name)
                {
                    if (chr == 0)
                        break;

                    if (chr >= 0x20 && chr <= 0x7E)
                        out.push_back(chr);
                    else
                        out.push_back('?');
                }
                return out;
            };

            for (std::size_t i = 0; i < hdr->parts; i++)
            {
                const auto *part = reinterpret_cast<gpt::partition_t *>(
                    entries_buf->data() + hdr->partentrysize * i
                );
                if (part->is_unused())
                    continue;

                lib::info(
                    "block: partition '{}': lba {}-{}{}",
                    ascii_name(part->name), part->lbastart, part->lbaend,
                    part->is_system() ? ", system" : part->is_boot() ? ", boot" : ""
                );

                add_part(part->lbastart, part->lbaend - part->lbastart + 1);
            }
        }
        else
        {
            // TODO: extended mbr

            lib::info("block: found mbr partition table");

            for (std::size_t i = 0; const auto &part : mbr->partitions)
            {
                if (part.is_unused())
                    continue;

                lib::info(
                    "block: partition {}: lba {}-{}{}",
                    i++, part.lbastart, part.lbastart + part.lbacount - 1,
                    part.is_bootable() ? ", boot" : ""
                );

                add_part(part.lbastart, part.lbacount);
            }
        }

        return { };
    }

    bool unregister_drive(std::shared_ptr<drive_t> drive)
    {
        for (const auto &part : drive->partitions())
        {
            if (!dev::unregister_device(part))
                return false;
        }

        if (!dev::unregister_device(drive->dev))
            return false;

        // TODO
        return true;
    }

    class_t &get_class()
    {
        static block_class_t blk { };
        return blk;
    }

    std::uint32_t alloc_minor()
    {
        static std::atomic_uint32_t next = 0;
        return next.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace dev::block
