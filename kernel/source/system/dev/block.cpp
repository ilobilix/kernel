// Copyright (C) 2024-2026  ilobilo

module system.dev.block;

import system.sched.wait_queue;
import system.vfs.dev;
import fmt;

namespace dev::block
{
    namespace
    {
        struct drive_data_t
        {
            std::weak_ptr<drive_t> drive;
        };

        struct part_data_t
        {
            std::size_t id;
            std::string name;
            std::weak_ptr<drive_t> drive;
        };

        class block_class_t : public class_t
        {
            public:
            block_class_t() : class_t { "block", empty_ktype(), true } { }

            std::string devnode(const device_t &dev, mode_t &mode) const override
            {
                lib::unused(mode);
                return dev.name;
            }
        };

        std::optional<std::uint64_t> to_sectors(std::uint64_t lbas, const drive_t &drive)
        {
            const auto block_size = drive.block_size();
            if (block_size < 512 || block_size % 512 != 0)
                return std::nullopt;

            const auto sectors_per_lba = block_size / 512;
            if (lbas > std::numeric_limits<std::uint64_t>::max() / sectors_per_lba)
                return std::nullopt;

            return lbas * sectors_per_lba;
        }

        struct disk_ktype_t : ktype_t
        {
            static std::shared_ptr<drive_t> drive_from(device_t &device)
            {
                if (!device.private_data)
                    return nullptr;

                return std::static_pointer_cast<drive_data_t>(device.private_data)->drive.lock();
            }

            std::span<attribute_t *const> attributes() const override
            {
                struct drive_attribute_t : make_attribute_t
                {
                    using rfn_t = lib::expect<std::string> (*)(
                        device_t &, std::shared_ptr<drive_t>
                    );
                    using wfn_t = lib::expect<void> (*)(
                        device_t &, std::shared_ptr<drive_t>, std::string_view
                    );

                    drive_attribute_t(rfn_t rfn, wfn_t wfn, std::string_view name, mode_t mode)
                        : make_attribute_t {
                            [rfn](device_t &dev) -> lib::expect<std::string> {
                                auto drive = drive_from(dev);
                                if (!drive)
                                    return std::unexpected { lib::err::io_error };
                                return rfn(dev, std::move(drive));
                            },
                            [wfn](device_t &dev, std::string_view value) -> lib::expect<void> {
                                auto drive = drive_from(dev);
                                if (!drive)
                                    return std::unexpected { lib::err::io_error };
                                return wfn(dev, std::move(drive), value);
                            }, name, mode
                        } { }
                };

                static drive_attribute_t size {
                    [](device_t &, std::shared_ptr<drive_t> drv) -> lib::expect<std::string> {
                        auto sectors = to_sectors(drv->block_count(), *drv);
                        if (!sectors)
                            return std::unexpected { lib::err::io_error };
                        return fmt::format("{}\n", *sectors);
                    }, nullptr, "size", 0444
                };
                static drive_attribute_t diskseq {
                    [](device_t &, std::shared_ptr<drive_t> drv) -> lib::expect<std::string> {
                        return fmt::format("{}\n", drv->seq());
                    }, nullptr, "diskseq", 0444
                };
                static drive_attribute_t ro {
                    [](device_t &, std::shared_ptr<drive_t> drv) -> lib::expect<std::string> {
                        lib::unused(drv);
                        return "0\n"; // TODO
                    }, nullptr, "ro", 0444
                };
                static drive_attribute_t removable {
                    [](device_t &, std::shared_ptr<drive_t> drv) -> lib::expect<std::string> {
                        lib::unused(drv);
                        return "0\n"; // TODO
                    }, nullptr, "removable", 0444
                };

                static attribute_t *list[] {
                    &size, &diskseq, &ro, &removable,
                    dev_attribute()
                };
                return list;
            }

            void fill_uevent(kobject_t &kobj, uevent_t &uev) override
            {
                auto device = kobj.as_device();
                if (!device)
                    return;

                auto drive = drive_from(*device);
                if (!drive)
                    return;

                uev.add("DEVTYPE", "disk");
                uev.add("DISKSEQ", std::to_string(drive->seq()));
            }
        };

        struct part_ktype_t : ktype_t
        {
            std::span<attribute_t *const> attributes() const override
            {
                struct part_attribute_t : make_attribute_t
                {
                    using rfn_t = lib::expect<std::string> (*)(
                        device_t &, std::shared_ptr<part_data_t>
                    );
                    using wfn_t = lib::expect<void> (*)(
                        device_t &, std::shared_ptr<part_data_t>, std::string_view
                    );

                    part_attribute_t(rfn_t rfn, wfn_t wfn, std::string_view name, mode_t mode)
                        : make_attribute_t {
                            [this, rfn](device_t &dev) -> lib::expect<std::string> {
                                if (!dev.private_data)
                                    return std::unexpected { lib::err::io_error };
                                if (!rfn)
                                    return attribute_t::show(dev);
                                return rfn(dev, std::static_pointer_cast<part_data_t>(dev.private_data));
                            },
                            [this, wfn](device_t &dev, std::string_view value) -> lib::expect<void> {
                                if (!dev.private_data)
                                    return std::unexpected { lib::err::io_error };
                                if (!wfn)
                                    return attribute_t::store(dev, value);
                                return wfn(dev, std::static_pointer_cast<part_data_t>(dev.private_data), value);
                            }, name, mode
                        } { }
                };

                static part_attribute_t size {
                    [](device_t &dev, std::shared_ptr<part_data_t> part) -> lib::expect<std::string> {
                        if (!dev.fops)
                            return std::unexpected { lib::err::io_error };

                        auto drive = part->drive.lock();
                        if (!drive)
                            return std::unexpected { lib::err::io_error };

                        auto ops = std::static_pointer_cast<ops_t>(dev.fops);
                        auto sectors = to_sectors(ops->get_memory().lba_count, *drive);
                        if (!sectors)
                            return std::unexpected { lib::err::io_error };

                        return fmt::format("{}\n", *sectors);
                    }, nullptr, "size", 0444
                };
                static part_attribute_t start {
                    [](device_t &dev, std::shared_ptr<part_data_t> part) -> lib::expect<std::string> {
                        if (!dev.fops)
                            return std::unexpected { lib::err::io_error };

                        auto drive = part->drive.lock();
                        if (!drive)
                            return std::unexpected { lib::err::io_error };

                        auto ops = std::static_pointer_cast<ops_t>(dev.fops);

                        auto sectors = to_sectors(ops->get_memory().lba_start, *drive);
                        if (!sectors)
                            return std::unexpected { lib::err::io_error };

                        return fmt::format("{}\n", *sectors);
                    }, nullptr, "start", 0444
                };
                static part_attribute_t partition {
                    [](device_t &, std::shared_ptr<part_data_t> part) -> lib::expect<std::string> {
                        return fmt::format("{}\n", part->id);
                    }, nullptr, "partition", 0444
                };
                static part_attribute_t removable {
                    [](device_t &, std::shared_ptr<part_data_t>) -> lib::expect<std::string> {
                        return "0\n"; // TODO
                    }, nullptr, "removable", 0444
                };

                static attribute_t *list[] {
                    &size, &start, &partition, &removable,
                    dev_attribute()
                };
                return list;
            }

            void fill_uevent(kobject_t &kobj, uevent_t &uev) override
            {
                auto device = kobj.as_device();
                if (!device)
                    return;

                if (!device->private_data)
                    return;
                auto part = std::static_pointer_cast<part_data_t>(device->private_data);

                auto drive = part->drive.lock();
                if (!drive)
                    return;

                uev.add("DEVTYPE", "partition");
                uev.add("PARTN", std::to_string(part->id));
                if (!part->name.empty())
                    uev.add("PARTNAME", part->name);
                uev.add("DISKSEQ", std::to_string(drive->seq()));
            }
        };

        ktype_t &get_part_ktype()
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
        drive->dev->add_ref_fn = [](auto &ref, auto &dev) {
            ref.add_link(block_root(), dev.name, dev.path());
        };
        drive->dev->rem_ref_fn = [](auto &ref, auto &dev) {
            ref.remove_link(block_root(), dev.name);
        };
        drive->dev->private_data = std::make_shared<drive_data_t>(drive);

        if (const auto ret = register_device(drive->dev); !ret)
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

        const auto add_part = [&](std::string name, std::uint64_t lba_start, std::uint64_t lba_count)
        {
            const auto max = drive->block_count();
            lib::bug_on(lba_start >= max || lba_count > max || lba_start > max - lba_count);

            const auto id = drive->_parts.size() + 1;
            auto part = device_t::create(
                fmt::format("{}{}{}", drive->dev->name, part_prefix, id),
                get_part_ktype(), drive->dev
            );
            part->cls = &get_class();
            part->devt = drive->alloc_id();
            part->fops = std::make_shared<ops_t>(drive, lba_start, lba_count);

            part->private_data = std::make_shared<part_data_t>(id, name, drive);

            drive->_parts.push_back(part);

            if (const auto ret = register_device(std::move(part)); !ret)
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

                add_part(ascii_name(part->name), part->lbastart, part->lbaend - part->lbastart + 1);
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

                add_part("", part.lbastart, part.lbacount);
            }
        }

        return { };
    }

    bool unregister_drive(std::shared_ptr<drive_t> drive)
    {
        for (const auto &part : drive->partitions())
        {
            if (!unregister_device(part))
                return false;
        }

        if (!unregister_device(drive->dev))
            return false;

        // TODO
        return true;
    }

    class_t &get_class()
    {
        static block_class_t blk { };
        return blk;
    }

    ktype_t &get_ktype()
    {
        static disk_ktype_t type { };
        return type;
    }

    std::uint32_t alloc_minor()
    {
        static std::atomic_uint32_t next = 0;
        return next.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace dev::block
