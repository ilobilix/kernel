// Copyright (C) 2024-2026  ilobilo

import system.memory.virt;
import system.sched;
import system.chrono;
import system.vfs.dev;
import system.vfs;
import lib;
import std;

import ext2;

namespace ext2
{
    namespace
    {
        constexpr std::uint32_t incompat_supported = feature_incompat_filetype;
        constexpr std::uint32_t ro_compat_supported = feature_ro_compat_sparse_super |
            feature_ro_compat_large_file;

        constexpr std::uint32_t max_log_block_size = 2;

        auto check_features(const superblock_t *sb, bool rw) -> lib::expect<void>
        {
            if ((sb->feature_incompat & incompat_supported) != incompat_supported)
                return std::unexpected { lib::err::invalid_argument };
            if (sb->feature_incompat & ~incompat_supported)
                return std::unexpected { lib::err::invalid_argument };
            if (rw && (sb->feature_ro_compat & ~ro_compat_supported))
                return std::unexpected { lib::err::invalid_argument };
            return { };
        }

        constexpr std::uint32_t dirent_reclen(std::uint8_t name_len)
        {
            return lib::align_up(sizeof(dir_entry_2_t) + name_len, dirent_align);
        }

        std::string_view dirent_name(const dir_entry_2_t *de)
        {
            return { de->name, de->name_len };
        }

        auto for_each_dirent(std::span<std::byte> data, auto &&fn) -> lib::expect<bool>
        {
            const std::uint32_t size = data.size();
            for (std::uint32_t i = 0; i < size; )
            {
                const auto de = reinterpret_cast<dir_entry_2_t *>(data.data() + i);
                const std::uint32_t rec = de->rec_len;

                if (rec < sizeof(dir_entry_2_t) || (rec % dirent_align) != 0 || i + rec > size)
                    return std::unexpected { lib::err::corrupted_data };
                if (sizeof(dir_entry_2_t) + de->name_len > rec)
                    return std::unexpected { lib::err::corrupted_data };

                const auto cont = fn(de, i);
                if (!cont.has_value())
                    return std::unexpected { cont.error() };
                if (!*cont)
                    return false;

                i += rec;
            }
            return true;
        }

        std::uint32_t now_secs()
        {
            return chrono::now(chrono::realtime).tv_sec;
        }

        std::uint8_t get_filetype(mode_t mode)
        {
            switch (stat::type(mode))
            {
                case stat::s_ifreg:
                    return ft_reg_file;
                case stat::s_ifdir:
                    return ft_dir;
                case stat::s_ifchr:
                    return ft_chrdev;
                case stat::s_ifblk:
                    return ft_blkdev;
                case stat::s_ififo:
                    return ft_fifo;
                case stat::s_ifsock:
                    return ft_sock;
                case stat::s_iflnk:
                    return ft_symlink;
                default:
                    return ft_unknown;
            }
        }

        bool has_block_map(mode_t mode, const ext2::inode_t *inode)
        {
            switch (stat::type(mode))
            {
                case stat::s_ifreg:
                case stat::s_ifdir:
                    return true;
                case stat::s_iflnk:
                    return inode->blocks != 0;
                default:
                    return false;
            }
        }

        void encode_dev(dev_t dev, std::span<std::uint32_t, num_blocks> block)
        {
            const auto maj = major(dev);
            const auto min = minor(dev);
            if (maj < 256 && min < 256)
            {
                block[0] = (maj << 8) | min;
                return;
            }

            block[0] = 0;
            block[1] = (min & 0xFF) | (maj << 8) | ((min & ~0xFFu) << 12);
        }

        dev_t decode_dev(std::span<const std::uint32_t, num_blocks> block)
        {
            if (block[0])
                return makedev((block[0] >> 8) & 0xFF, block[0] & 0xFF);

            const auto val = block[1];
            return makedev((val >> 8) & 0xFFF, (val & 0xFF) | ((val >> 12) & ~0xFFu));
        }

        struct instance_t;
        using instance_ptr = lib::locked_ptr<instance_t, sched::mutex_t>;

        struct fs_inode_t;
        struct instance_t : vfs::filesystem_t::instance_t
        {
            std::shared_ptr<vfs::file_t> src;
            lib::buffer<superblock_t> sb_buf;
            lib::buffer<group_desc_t> gds;

            std::uint32_t block_size;
            std::uint16_t inode_size;

            lib::map::flat_hash<ino_t, std::weak_ptr<fs_inode_t>> icache;
            std::uint64_t flags;

            std::vector<std::uint32_t> block_hints;
            std::vector<std::uint32_t> inode_hints;

            sched::mutex_t io_lock;

            instance_t(
                std::shared_ptr<vfs::file_t> src,
                lib::buffer<superblock_t> sb, lib::buffer<group_desc_t> gds,
                std::uint32_t block_size, std::uint16_t inode_size, std::uint64_t flags
            ) : src { std::move(src) }, sb_buf { std::move(sb) }, gds { std::move(gds) },
                block_size { block_size }, inode_size { inode_size }, flags { flags },
                block_hints (this->gds.size(), 0), inode_hints (this->gds.size(), 0) { }

            auto superblock(this auto &&self) { return self.sb_buf.data(); }

            bool read_only() const { return flags & vfs::ms_rdonly; }

            std::uint32_t ptrs_per_block() const { return block_size / sizeof(std::uint32_t); }
            std::uint32_t sectors_per_block() const { return block_size / 512; }
            std::uint32_t group_count() const { return gds.size(); }

            std::uint64_t gdt_offset() const
            {
                return static_cast<std::uint64_t>(superblock()->first_data_block + 1) * block_size;
            }

            std::uint32_t blocks_in_group(std::uint32_t group) const
            {
                const auto sb = superblock();
                const std::uint64_t total = sb->blocks_count - sb->first_data_block;
                const auto base = static_cast<std::uint64_t>(group) * sb->blocks_per_group;
                return std::min<std::uint64_t>(sb->blocks_per_group, total - base);
            }

            std::uint32_t inodes_in_group(std::uint32_t group) const
            {
                const auto sb = superblock();
                const auto base = static_cast<std::uint64_t>(group) * sb->inodes_per_group;
                return std::min<std::uint64_t>(sb->inodes_per_group, sb->inodes_count - base);
            }

            void mark_clean()
            {
                const std::unique_lock _ { io_lock };
                superblock()->state |= state_clean;
            }

            template<typename Type>
            auto read_at(std::uint64_t offset, std::size_t count = 1)
                -> lib::expect<lib::buffer<Type>>
            {
                return src->read_obj<Type>(offset, count);
            }

            std::uint64_t inode_offset(std::uint32_t ino) const
            {
                const auto ipg = superblock()->inodes_per_group;
                const auto group = (ino - 1) / ipg;
                const auto index = (ino - 1) % ipg;
                return static_cast<std::uint64_t>(gds.at(group).inode_table) * block_size +
                    static_cast<std::uint64_t>(index) * inode_size;
            }

            auto read_ino(ino_t ino) -> lib::expect<lib::buffer<inode_t>>
            {
                return read_at<inode_t>(inode_offset(ino));
            }

            auto iget(const instance_ptr &handle, ino_t ino)
                -> lib::expect<std::shared_ptr<fs_inode_t>>;

            auto bmap(ext2::inode_t *inode, std::uint32_t num)
                -> lib::expect<std::pair<std::uint32_t, std::uint32_t>>
            {
                const std::uint64_t ppb = ptrs_per_block();

                auto follow = [&](std::uint32_t blk, std::uint64_t idx)
                    -> lib::expect<std::uint32_t>
                {
                    if (blk == 0)
                        return 0;

                    auto res = read_at<std::uint32_t>(
                        static_cast<std::uint64_t>(blk) * block_size, ppb
                    );
                    if (!res.has_value())
                        return std::unexpected { res.error() };
                    return res->at(idx);
                };

                auto run_from = [&](std::span<const std::uint32_t> leaf, std::uint64_t idx)
                    -> std::pair<std::uint32_t, std::uint32_t>
                {
                    const auto phys = leaf[idx];
                    std::uint32_t count = 1;
                    while (idx + count < leaf.size() &&
                        leaf[idx + count] == (phys == 0 ? 0 : phys + count))
                        count++;
                    return { phys, count };
                };

                if (num < ndir_blocks)
                    return run_from({ inode->block, ndir_blocks }, num);
                num -= ndir_blocks;

                if (num < ppb)
                {
                    if (inode->block[ind_block] == 0)
                        return std::make_pair(0, ppb - num);

                    auto buf = read_at<std::uint32_t>(
                        static_cast<std::uint64_t>(inode->block[ind_block]) * block_size, ppb
                    );
                    if (!buf.has_value())
                        return std::unexpected { buf.error() };
                    return run_from(buf->span(), num);
                }
                num -= ppb;

                if (num < ppb * ppb)
                {
                    auto l1 = follow(inode->block[dind_block], num / ppb);
                    if (!l1.has_value())
                        return std::unexpected { l1.error() };

                    const auto idx = num % ppb;
                    if (*l1 == 0)
                        return std::make_pair(0, ppb - idx);

                    auto buf = read_at<std::uint32_t>(
                        static_cast<std::uint64_t>(*l1) * block_size, ppb
                    );
                    if (!buf.has_value())
                        return std::unexpected { buf.error() };
                    return run_from(buf->span(), idx);
                }
                num -= ppb * ppb;

                if (num < ppb * ppb * ppb)
                {
                    auto l1 = follow(inode->block[tind_block], num / (ppb * ppb));
                    if (!l1.has_value())
                        return std::unexpected { l1.error() };

                    auto l2 = follow(*l1, (num / ppb) % ppb);
                    if (!l2.has_value())
                        return std::unexpected { l2.error() };

                    const auto idx = num % ppb;
                    if (*l2 == 0)
                        return std::make_pair(0, ppb - idx);

                    auto buf = read_at<std::uint32_t>(
                        static_cast<std::uint64_t>(*l2) * block_size, ppb
                    );
                    if (!buf.has_value())
                        return std::unexpected { buf.error() };
                    return run_from(buf->span(), idx);
                }

                return std::unexpected { lib::err::invalid_argument };
            }

            auto for_each_run(
                ext2::inode_t *inode, std::uint64_t offset, std::uint64_t total, auto &&fn
            ) -> lib::expect<void>
            {
                std::uint64_t progress = 0;
                while (progress < total)
                {
                    const auto pos = offset + progress;
                    const auto boff = pos % block_size;

                    auto res = bmap(inode, pos / block_size);
                    if (!res.has_value())
                        return std::unexpected { res.error() };
                    const auto [phys, run] = *res;

                    const auto run_bytes = static_cast<std::uint64_t>(run) * block_size - boff;
                    const auto chunk = std::min(run_bytes, total - progress);

                    const auto ret = fn(
                        progress,
                        phys ? static_cast<std::uint64_t>(phys) * block_size + boff : 0,
                        chunk
                    );
                    if (!ret.has_value())
                        return std::unexpected { ret.error() };

                    progress += chunk;
                }
                return { };
            }

            auto read_data(
                ext2::inode_t *inode, std::uint64_t offset,
                lib::maybe_uspan<std::byte> dst
            ) -> lib::expect<std::size_t>
            {
                const auto total = dst.size_bytes();
                const auto ret = for_each_run(inode, offset, total,
                    [&](std::uint64_t at, std::uint64_t src, std::uint64_t len)
                        -> lib::expect<void>
                    {
                        auto out = dst.subspan(at, len);
                        if (src == 0)
                        {
                            if (!out.fill(0, len))
                                return std::unexpected { lib::err::invalid_address };
                            return { };
                        }

                        auto blk = read_at<std::byte>(src, len);
                        if (!blk.has_value())
                            return std::unexpected { blk.error() };
                        if (!out.copy_from(blk->span()))
                            return std::unexpected { lib::err::invalid_address };
                        return { };
                    }
                );
                if (!ret.has_value())
                    return std::unexpected { ret.error() };
                return total;
            }

            auto for_each_dir_block(fs_inode_t *dir, auto &&fn) -> lib::expect<void>;

            auto walk_dir(fs_inode_t *dir, std::uint64_t start, auto &&fn) -> lib::expect<void>;

            auto write_bytes(std::uint64_t offset, std::span<const std::byte> data)
                -> lib::expect<void>
            {
                const auto uspan = lib::maybe_uspan<std::byte>::create(
                    const_cast<std::byte *>(data.data()), data.size()
                );
                lib::bug_on(!uspan.has_value());

                const auto ret = src->pwrite(offset, *uspan);
                if (!ret.has_value())
                    return std::unexpected { ret.error() };
                if (*ret != data.size())
                    return std::unexpected { lib::err::io_error };
                return { };
            }

            auto zero_block(std::uint32_t blk) -> lib::expect<void>
            {
                const lib::membuffer buf { block_size, lib::zeroed };
                return write_bytes(static_cast<std::uint64_t>(blk) * block_size, buf.span());
            }

            auto bitmap_alloc(std::uint32_t bitmap_blk, std::uint32_t count, std::uint32_t &hint)
                -> lib::expect<std::optional<std::uint32_t>>
            {
                const auto off = static_cast<std::uint64_t>(bitmap_blk) * block_size;
                auto bm = read_at<std::byte>(off, block_size);
                if (!bm.has_value())
                    return std::unexpected { bm.error() };

                auto bits = reinterpret_cast<std::uint8_t *>(bm->data());

                const auto scan = [&](std::uint32_t from, std::uint32_t to) -> std::optional<std::uint32_t>
                {
                    for (std::uint32_t bit = from; bit < to; )
                    {
                        if ((bit & 7) == 0 && bits[bit >> 3] == 0xFF)
                        {
                            bit += 8;
                            continue;
                        }

                        if (!(bits[bit >> 3] & (1u << (bit & 7))))
                            return bit;
                        bit++;
                    }
                    return std::nullopt;
                };

                if (hint >= count)
                    hint = 0;

                auto found = scan(hint, count);
                if (!found)
                    found = scan(0, hint);
                if (!found)
                    return std::nullopt;

                const auto bit = *found;
                bits[bit >> 3] |= (1u << (bit & 7));

                if (const auto ret = write_bytes(off, bm->span()); !ret.has_value())
                    return std::unexpected { ret.error() };

                hint = bit + 1;
                return bit;
            }

            auto bitmap_free(std::uint32_t bitmap_blk, std::uint32_t bit) -> lib::expect<bool>
            {
                const auto off = static_cast<std::uint64_t>(bitmap_blk) * block_size;
                auto bm = read_at<std::byte>(off, block_size);
                if (!bm.has_value())
                    return std::unexpected { bm.error() };

                auto bits = reinterpret_cast<std::uint8_t *>(bm->data());
                if (!(bits[bit >> 3] & (1u << (bit & 7))))
                    return false;
                bits[bit >> 3] &= ~(1u << (bit & 7));

                if (const auto ret = write_bytes(off, bm->span()); !ret.has_value())
                    return std::unexpected { ret.error() };
                return true;
            }

            auto commit_group(std::uint32_t group) -> lib::expect<void>
            {
                const auto off = gdt_offset() + group * sizeof(group_desc_t);
                return write_bytes(off, std::as_bytes(gds.span().subspan(group, 1)));
            }

            auto alloc_block(std::uint32_t target_group) -> lib::expect<std::uint32_t>
            {
                auto sb = superblock();
                if (sb->free_blocks_count == 0)
                    return 0;

                if (sb->free_blocks_count <= sb->r_blocks_count && sched::current_process()->cred->euid)
                    return 0;

                for (std::uint32_t groups = group_count(), i = 0; i < groups; i++)
                {
                    const auto group = (target_group + i) % groups;
                    if (gds.at(group).free_blocks_count == 0)
                        continue;

                    auto bit = bitmap_alloc(
                        gds.at(group).block_bitmap,
                        blocks_in_group(group),
                        block_hints.at(group)
                    );
                    if (!bit.has_value())
                        return std::unexpected { bit.error() };
                    if (!bit->has_value())
                        continue;

                    gds.at(group).free_blocks_count--;
                    sb->free_blocks_count--;
                    if (const auto ret = commit_group(group); !ret.has_value())
                        return std::unexpected { ret.error() };

                    return group * sb->blocks_per_group + sb->first_data_block + **bit;
                }
                return 0;
            }

            auto free_block(std::uint32_t phys) -> lib::expect<void>
            {
                auto sb = superblock();
                if (phys < sb->first_data_block || phys >= sb->blocks_count)
                    return std::unexpected { lib::err::corrupted_data };

                const auto rel = phys - sb->first_data_block;
                const auto group = rel / sb->blocks_per_group;
                if (group >= group_count())
                    return std::unexpected { lib::err::corrupted_data };

                const auto bit = rel % sb->blocks_per_group;
                auto freed = bitmap_free(gds.at(group).block_bitmap, bit);
                if (!freed.has_value())
                    return std::unexpected { freed.error() };

                if (*freed)
                {
                    gds.at(group).free_blocks_count++;
                    sb->free_blocks_count++;
                    block_hints.at(group) = std::min(block_hints.at(group), bit);
                    return commit_group(group);
                }
                return { };
            }

            auto alloc_inode(std::uint32_t target_group, bool is_dir) -> lib::expect<std::uint32_t>
            {
                auto sb = superblock();
                if (sb->free_inodes_count == 0)
                    return 0;

                for (std::uint32_t groups = group_count(), i = 0; i < groups; i++)
                {
                    const auto group = (target_group + i) % groups;
                    if (gds.at(group).free_inodes_count == 0)
                        continue;

                    auto bit = bitmap_alloc(
                        gds.at(group).inode_bitmap,
                        inodes_in_group(group),
                        inode_hints.at(group)
                    );
                    if (!bit.has_value())
                        return std::unexpected { bit.error() };
                    if (!bit->has_value())
                        continue;

                    gds.at(group).free_inodes_count--;
                    sb->free_inodes_count--;
                    if (is_dir)
                        gds.at(group).used_dirs_count++;
                    if (const auto ret = commit_group(group); !ret.has_value())
                        return std::unexpected { ret.error() };

                    return group * sb->inodes_per_group + **bit + 1;
                }
                return 0;
            }

            auto free_inode(std::uint32_t ino, bool was_dir) -> lib::expect<void>
            {
                auto sb = superblock();
                if (ino == 0 || ino > sb->inodes_count)
                    return std::unexpected { lib::err::corrupted_data };

                const auto group = (ino - 1) / sb->inodes_per_group;
                if (group >= group_count())
                    return std::unexpected { lib::err::corrupted_data };

                const auto bit = (ino - 1) % sb->inodes_per_group;
                auto freed = bitmap_free(gds.at(group).inode_bitmap, bit);
                if (!freed.has_value())
                    return std::unexpected { freed.error() };

                if (*freed)
                {
                    gds.at(group).free_inodes_count++;
                    sb->free_inodes_count++;
                    if (was_dir && gds.at(group).used_dirs_count > 0)
                        gds.at(group).used_dirs_count--;
                    inode_hints.at(group) = std::min(inode_hints.at(group), bit);
                    return commit_group(group);
                }
                return { };
            }

            auto write_inode_raw(std::uint32_t ino, const ext2::inode_t &disk) -> lib::expect<void>
            {
                return write_bytes(inode_offset(ino), std::as_bytes(std::span { &disk, 1 }));
            }

            auto bmap_alloc(fs_inode_t *finode, std::uint32_t lblk)
                -> lib::expect<std::pair<std::uint32_t, bool>>;
            auto free_indirect(
                std::uint32_t blk, std::size_t level, std::uint64_t base,
                std::uint64_t from, fs_inode_t *finode
            ) -> lib::expect<bool>;
            auto truncate_blocks(fs_inode_t *finode, std::uint64_t new_size) -> lib::expect<void>;

            void set_size(fs_inode_t *finode, std::uint64_t size);
            void fold_stat_to_ino(fs_inode_t *finode);

            auto write_inode_impl(fs_inode_t *finode) -> lib::expect<void>;

            auto flush_metadata() -> lib::expect<void>;
            auto collect_live() -> std::vector<std::shared_ptr<fs_inode_t>>;
            void flush_dirty_inodes(std::span<const std::shared_ptr<fs_inode_t>> live);
            void flush_all();

            auto dirent_lookup(fs_inode_t *dir, std::string_view name)
                -> lib::expect<std::optional<std::uint32_t>>;
            auto dirent_set(
                fs_inode_t *dir, std::string_view name,
                std::uint32_t ino, std::uint8_t ft
            ) -> lib::expect<bool>;
            auto dirent_insert(
                fs_inode_t *dir, std::string_view name,
                std::uint32_t ino, std::uint8_t ft
            ) -> lib::expect<void>;
            auto dirent_remove(fs_inode_t *dir, std::string_view name) -> lib::expect<void>;

            auto free_everything(fs_inode_t *finode) -> lib::expect<void>;

            auto create(
                std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                mode_t mode, dev_t rdev, std::optional<std::shared_ptr<vfs::ops_t>> ops
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override;

            auto symlink(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override;

            auto link(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, std::shared_ptr<vfs::inode_t> target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override;

            auto unlink(
                std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                std::shared_ptr<vfs::inode_t> &inode
            ) -> lib::expect<void> override;

            auto rename(
                std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode_t> replaced
            ) -> lib::expect<void> override;

            auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override;

            auto lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
                -> lib::expect<vfs::dir_entry> override;

            auto readlink(std::shared_ptr<vfs::dentry_t> dentry) -> lib::expect<lib::path> override;

            void statfs(struct ::statfs &out) override
            {
                vfs::filesystem_t::instance_t::statfs(out);

                const std::unique_lock _ { io_lock };
                const auto sb = superblock();

                out.f_bsize = block_size;
                out.f_frsize = block_size;
                out.f_blocks = sb->blocks_count;
                out.f_bfree = sb->free_blocks_count;
                out.f_bavail = sb->free_blocks_count > sb->r_blocks_count
                    ? sb->free_blocks_count - sb->r_blocks_count : 0;
                out.f_files = sb->inodes_count;
                out.f_ffree = sb->free_inodes_count;
                out.f_namelen = name_len;
            }

            auto write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override;
            auto dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override;

            auto getxattr(std::shared_ptr<vfs::inode_t> &inode, std::string_view name)
                -> lib::expect<lib::membuffer> override
            {
                // TODO
                lib::unused(inode, name);
                return std::unexpected { lib::err::not_supported };
            }

            auto setxattr(
                std::shared_ptr<vfs::inode_t> &inode, std::string_view name,
                lib::maybe_uspan<std::byte> data, int flags
            ) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode, name, data, flags);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto remxattr(std::shared_ptr<vfs::inode_t> &inode, std::string_view name)
                -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode, name);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto listxattrs(std::shared_ptr<vfs::inode_t> &inode)
                -> lib::expect<std::vector<std::string>> override
            {
                // TODO
                lib::unused(inode);
                return std::unexpected { lib::err::not_supported };
            }

            bool sync() override;
            bool unmount(std::shared_ptr<vfs::mount_t> mnt) override;
            bool remount(std::uint64_t new_flags) override;
        };

        struct ops_t : vfs::ops_t
        {
            bool truncable() const override { return true; }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override;

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override;

            lib::expect<void> trunc(std::shared_ptr<vfs::file_t> file, std::size_t size) override;

            lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file_t> file) override;

            lib::expect<void> sync(std::shared_ptr<vfs::file_t> file, bool datasync) override;

            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }
        };

        struct fs_inode_t : vfs::inode_t
        {
            instance_t *owner;
            instance_ptr handle;
            lib::buffer<ext2::inode_t> ino_buf;

            std::weak_ptr<fs_inode_t> self;

            vmm::object::ptr data;
            bool destroy = false;

            ext2::inode_t *inode() { return ino_buf.data(); }

            static std::shared_ptr<vfs::ops_t> get_ops(mode_t mode)
            {
                switch (stat::type(mode))
                {
                    case stat::s_iflnk:
                    case stat::s_ifreg:
                    case stat::s_ifdir:
                        return ops_t::singleton();
                    default:
                        return nullptr;
                }
            }

            static auto create(
                instance_t *owner, instance_ptr handle,
                lib::buffer<ext2::inode_t> disk, ino_t id,
                std::optional<std::shared_ptr<vfs::ops_t>> ops = std::nullopt
            ) -> std::shared_ptr<fs_inode_t>
            {
                const auto mode = disk.data()->mode;
                auto node = std::make_shared<fs_inode_t>(
                    owner, std::move(handle), std::move(disk), id,
                    ops.has_value() ? std::move(*ops) : get_ops(mode)
                );
                node->self = node;
                return node;
            }

            fs_inode_t(
                instance_t *owner, instance_ptr handle, lib::buffer<ext2::inode_t> disk,
                ino_t id, std::shared_ptr<vfs::ops_t> ops
            ) : vfs::inode_t { std::move(ops) }, owner { owner },
                handle { std::move(handle) }, ino_buf { std::move(disk) }
            {
                const auto *ino = inode();

                stat.st_dev = owner->dev_id;
                stat.st_ino = id;
                stat.st_nlink = ino->links_count;
                stat.st_mode = ino->mode;
                stat.st_uid = ino->uid | (static_cast<uid_t>(ino->osd2.linux2.uid_high) << 16);
                stat.st_gid = ino->gid | (static_cast<gid_t>(ino->osd2.linux2.gid_high) << 16);

                if (stat.type() == stat::s_ifchr || stat.type() == stat::s_ifblk)
                    stat.st_rdev = decode_dev(ino->block);
                else
                    stat.st_rdev = 0;

                stat.st_size = ino->size;
                if (stat.type() == stat::s_ifreg)
                    stat.st_size |= static_cast<std::uint64_t>(ino->dir_acl) << 32;
                stat.st_blksize = owner->block_size;
                stat.st_blocks = ino->blocks;

                stat.st_atim = timespec { ino->atime, 0 };
                stat.st_mtim = timespec { ino->mtime, 0 };
                stat.st_ctim = timespec { ino->ctime, 0 };

                // TODO: flags
            }

            ~fs_inode_t();
        };

        fs_inode_t *inode_of(const std::shared_ptr<vfs::file_t> &file)
        {
            return static_cast<fs_inode_t *>(file->path.dentry->inode.get());
        }

        fs_inode_t *inode_of(const std::shared_ptr<vfs::inode_t> &inode)
        {
            return static_cast<fs_inode_t *>(inode.get());
        }

        fs_inode_t *inode_of(const std::shared_ptr<vfs::dentry_t> &dentry)
        {
            return static_cast<fs_inode_t *>(dentry->inode.get());
        }

        auto instance_t::for_each_dir_block(fs_inode_t *dir, auto &&fn) -> lib::expect<void>
        {
            const std::uint64_t size = dir->stat.st_size;
            for (std::uint64_t off = 0; off < size; off += block_size)
            {
                auto res = bmap(dir->inode(), off / block_size);
                if (!res.has_value())
                    return std::unexpected { res.error() };

                const std::uint64_t phys = res->first;
                if (phys == 0)
                    continue;

                auto blk = read_at<std::byte>(phys * block_size, block_size);
                if (!blk.has_value())
                    return std::unexpected { blk.error() };

                const auto cont = fn(phys, blk->span());
                if (!cont.has_value())
                    return std::unexpected { cont.error() };
                if (!*cont)
                    return { };
            }
            return { };
        }

        auto instance_t::walk_dir(fs_inode_t *dir, std::uint64_t start, auto &&fn)
            -> lib::expect<void>
        {
            const std::uint64_t size = dir->stat.st_size;
            for (std::uint64_t off = start; off < size; )
            {
                const auto lblk = off / block_size;
                const auto block_start = lblk * block_size;

                auto res = bmap(dir->inode(), lblk);
                if (!res.has_value())
                    return std::unexpected { res.error() };

                if (const std::uint64_t phys = res->first; phys != 0)
                {
                    auto blk = read_at<std::byte>(phys * block_size, block_size);
                    if (!blk.has_value())
                        return std::unexpected { blk.error() };

                    const auto cont = for_each_dirent(blk->span(),
                        [&](dir_entry_2_t *de, std::uint32_t i) -> lib::expect<bool>
                        {
                            const auto abs = block_start + i;
                            if (abs < start || de->inode == 0)
                                return true;

                            const auto name = dirent_name(de);
                            if (name == "." || name == "..")
                                return true;

                            return fn(name, de->inode, abs);
                        }
                    );
                    if (!cont.has_value())
                        return std::unexpected { cont.error() };
                    if (!*cont)
                        return { };
                }

                off = block_start + block_size;
            }
            return { };
        }

        struct object_t : vmm::object
        {
            std::weak_ptr<fs_inode_t> finode;

            lib::expect<void> fetch_pages(std::size_t idx, std::span<vmm::page *> pages) override
            {
                auto inode = finode.lock();
                if (!inode)
                    return std::unexpected { lib::err::invalid_device_or_address };

                const auto npsize = vmm::default_npsize();

                auto fs = inode->owner;
                const std::unique_lock _ { fs->io_lock };

                const std::uint64_t file_size = inode->stat.st_size;
                const std::uint64_t base = static_cast<std::uint64_t>(idx) * npsize;
                const std::uint64_t total = static_cast<std::uint64_t>(pages.size()) * npsize;

                const auto fill = [&](std::uint64_t at, std::uint64_t len, const std::byte *src) {
                    while (len != 0)
                    {
                        const auto poff = at % npsize;
                        const auto take = std::min<std::uint64_t>(npsize - poff, len);
                        auto dst = reinterpret_cast<std::byte *>(
                            lib::tohh(vmm::paddr_from(pages[at / npsize]))
                        ) + poff;

                        if (src)
                        {
                            std::memcpy(dst, src, take);
                            src += take;
                        }
                        else std::memset(dst, 0, take);

                        at += take;
                        len -= take;
                    }
                };

                const auto valid = base < file_size
                    ? std::min(total, file_size - base) : 0ul;
                if (valid != total)
                    fill(valid, total - valid, nullptr);

                return fs->for_each_run(inode->inode(), base, valid,
                    [&](std::uint64_t at, std::uint64_t src, std::uint64_t len) -> lib::expect<void>
                    {
                        if (src == 0)
                        {
                            fill(at, len, nullptr);
                            return { };
                        }

                        auto blk = fs->read_at<std::byte>(src, len);
                        if (!blk.has_value())
                            return std::unexpected { blk.error() };

                        fill(at, len, blk->data());
                        return { };
                    }
                );
            }

            lib::expect<void> write_pages(std::size_t idx, std::span<vmm::page *> pages) override
            {
                auto inode = finode.lock();
                if (!inode)
                    return std::unexpected { lib::err::invalid_device_or_address };

                const auto npsize = vmm::default_npsize();

                auto fs = inode->owner;
                if (fs->read_only())
                    return std::unexpected { lib::err::read_only_fs };

                const std::unique_lock _ { fs->io_lock };

                const std::uint64_t file_size = inode->stat.st_size;
                const std::uint64_t bs = fs->block_size;
                bool allocated_any = false;

                for (std::size_t i = 0; i < pages.size(); i++)
                {
                    const auto page_off = (idx + i) * npsize;
                    if (page_off >= file_size)
                        continue;

                    auto base = reinterpret_cast<std::byte *>(lib::tohh(vmm::paddr_from(pages[i])));
                    for (std::uint64_t bo = 0; bo < npsize; bo += bs)
                    {
                        const auto block_off = page_off + bo;
                        if (block_off >= file_size)
                            break;

                        auto pr = fs->bmap_alloc(inode.get(), block_off / bs);
                        if (!pr.has_value())
                            return std::unexpected { pr.error() };
                        allocated_any |= pr->second;

                        const auto ret = fs->write_bytes(
                            static_cast<std::uint64_t>(pr->first) * bs, { base + bo, bs }
                        );
                        if (!ret.has_value())
                            return std::unexpected { ret.error() };
                    }
                }

                if (allocated_any)
                {
                    inode->stat.st_blocks = inode->inode()->blocks;
                    if (const auto ret = fs->write_inode_impl(inode.get()); !ret.has_value())
                        return ret;
                }
                return { };
            }

            object_t(fs_inode_t *inode)
                : vmm::object { vmm::object_type::file }, finode { inode->self } { }
        };

        vmm::object::ptr get_object(fs_inode_t *finode)
        {
            const std::unique_lock _ { finode->lock };
            if (!finode->data)
                finode->data = new object_t { finode };
            return finode->data;
        }

        lib::expect<std::size_t> ops_t::read(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        )
        {
            const auto finode = inode_of(file);

            const auto file_size = static_cast<std::uint64_t>(finode->stat.st_size);
            if (offset >= file_size)
                return 0;

            const auto real_size = std::min(buffer.size_bytes(), file_size - offset);
            if (real_size == 0)
                return 0;

            return get_object(finode)->read(offset, buffer.subspan(0, real_size));
        }

        lib::expect<std::size_t> ops_t::write(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        )
        {
            const auto finode = inode_of(file);
            const auto fs = finode->owner;
            if (fs->read_only())
                return std::unexpected { lib::err::read_only_fs };

            auto obj = get_object(finode);
            const std::unique_lock _ { finode->lock };

            if (file->flags & vfs::o_append)
            {
                offset = finode->stat.st_size;
                file->offset = offset;
            }

            if (buffer.size_bytes() == 0)
                return 0;

            const auto num = obj->write(offset, buffer);
            if (num == 0)
                return std::unexpected { lib::err::io_error };

            const auto new_end = offset + num;
            {
                const std::unique_lock _ { fs->io_lock };

                if (new_end > static_cast<std::uint64_t>(finode->stat.st_size))
                    fs->set_size(finode, new_end);

                finode->stat.update_time(kstat::time::modify | kstat::time::status);
                finode->dirty = true;
            }

            if (file->flags & vfs::o_direct)
            {
                const auto npsize = vmm::default_npsize();
                const auto ret = obj->write_back(
                    offset / npsize,
                    lib::div_roundup(new_end, npsize) - offset / npsize
                );
                if (!ret.has_value())
                    return std::unexpected { ret.error() };
            }
            return num;
        }

        lib::expect<void> ops_t::trunc(std::shared_ptr<vfs::file_t> file, std::size_t size)
        {
            const auto finode = inode_of(file);
            const auto fs = finode->owner;
            if (fs->read_only())
                return std::unexpected { lib::err::read_only_fs };

            const std::unique_lock _ { finode->lock };
            const std::uint64_t old_size = finode->stat.st_size;

            {
                const std::unique_lock _ { fs->io_lock };

                if (size < old_size)
                {
                    if (const auto ret = fs->truncate_blocks(finode, size); !ret.has_value())
                        return ret;
                }

                fs->set_size(finode, size);
                finode->stat.st_blocks = finode->inode()->blocks;
                finode->stat.update_time(kstat::time::modify | kstat::time::status);

                if (const auto ret = fs->write_inode_impl(finode); !ret.has_value())
                    return ret;
            }

            if (size < old_size && finode->data)
            {
                const auto npsize = vmm::default_npsize();
                if (const auto tail = size % npsize; tail != 0)
                {
                    finode->data->clear(size, 0, npsize - tail);
                    if (const auto ret = finode->data->write_back(size / npsize, 1);
                        !ret.has_value())
                        return ret;
                }
                finode->data->drop_cached(lib::div_roundup<std::uint64_t>(size, npsize), ~0ul);
            }
            return { };
        }

        lib::expect<vmm::object::ptr> ops_t::map(std::shared_ptr<vfs::file_t> file)
        {
            const auto &dentry = file->path.dentry;
            if (!dentry || !dentry->inode)
                return std::unexpected { lib::err::no_such_device };

            const auto finode = inode_of(file);
            if (finode->stat.type() != stat::s_ifreg)
                return std::unexpected { lib::err::no_such_device };

            return get_object(finode);
        }

        lib::expect<void> ops_t::sync(std::shared_ptr<vfs::file_t> file, bool datasync)
        {
            lib::unused(datasync);

            const auto finode = inode_of(file);
            const auto owner = finode->owner;
            if (owner->read_only())
                return { };

            if (finode->data)
            {
                const auto npsize = vmm::default_npsize();
                const auto pages = lib::div_roundup<std::uint64_t>(finode->stat.st_size, npsize);
                if (pages != 0)
                {
                    if (const auto ret = finode->data->write_back(0, pages); !ret.has_value())
                        return ret;
                }
            }

            {
                const std::unique_lock _ { owner->io_lock };
                if (finode->dirty)
                {
                    if (const auto ret = owner->write_inode_impl(finode); !ret.has_value())
                        return ret;
                }

                if (const auto ret = owner->flush_metadata(); !ret.has_value())
                    return ret;
            }

            return owner->src->sync();
        }

        auto instance_t::iget(const instance_ptr &handle, ino_t ino)
            -> lib::expect<std::shared_ptr<fs_inode_t>>
        {
            if (ino == 0 || ino > superblock()->inodes_count)
                return std::unexpected { lib::err::corrupted_data };

            if (const auto it = icache.find(ino); it != icache.end())
            {
                if (auto sp = it->second.lock())
                    return sp;
            }

            auto res = read_ino(ino);
            if (!res.has_value())
                return std::unexpected { res.error() };

            auto node = fs_inode_t::create(this, handle, std::move(*res), ino);
            icache.insert_or_assign(ino, node);
            return node;
        }

        auto instance_t::readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
            -> lib::expect<lib::list<vfs::dir_entry>>
        {
            constexpr std::size_t max_batch = 256;
            lib::list<vfs::dir_entry> out;

            const auto dirnode = inode_of(dir);
            const auto ret = walk_dir(dirnode, cookie,
                [&](std::string_view name, ino_t ino, std::uint64_t off) -> lib::expect<bool>
                {
                    auto child = iget(dirnode->handle, ino);
                    if (!child.has_value())
                        return std::unexpected { child.error() };

                    out.push_back({ std::string { name }, std::move(*child), off });
                    return out.size() < max_batch;
                }
            );
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            return out;
        }

        auto instance_t::lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
            -> lib::expect<vfs::dir_entry>
        {
            std::shared_ptr<fs_inode_t> found;
            const auto dirnode = inode_of(dir);
            const auto ret = walk_dir(dirnode, 0,
                [&](std::string_view ename, ino_t ino, std::uint64_t off) -> lib::expect<bool>
                {
                    lib::unused(off);

                    if (ename != name)
                        return true;

                    auto child = iget(dirnode->handle, ino);
                    if (!child.has_value())
                        return std::unexpected { child.error() };

                    found = std::move(*child);
                    return false;
                }
            );
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            if (!found)
                return std::unexpected { lib::err::not_found };
            return vfs::dir_entry { std::string { name }, std::move(found), 0 };
        }

        auto instance_t::readlink(std::shared_ptr<vfs::dentry_t> dentry) -> lib::expect<lib::path>
        {
            const auto finode = inode_of(dentry);
            const auto ino = finode->inode();
            const std::uint64_t size = finode->stat.st_size;

            if (size < fast_symlink_size && ino->blocks == 0)
                return std::string_view { reinterpret_cast<const char *>(ino->block), size };

            lib::membuffer buf { size };
            const auto us = buf.byte_uspan();
            lib::bug_on(!us);

            if (const auto ret = read_data(ino, 0, *us); !ret.has_value())
                return std::unexpected { ret.error() };
            return std::string_view { reinterpret_cast<const char *>(buf.data()), size };
        }

        auto instance_t::bmap_alloc(fs_inode_t *finode, std::uint32_t lblk)
            -> lib::expect<std::pair<std::uint32_t, bool>>
        {
            const auto ino = finode->inode();
            const auto goal = (finode->stat.st_ino - 1) / superblock()->inodes_per_group;
            const auto ppb = ptrs_per_block();
            const auto spb = sectors_per_block();

            const auto alloc_one = [&](bool child_meta) -> lib::expect<std::uint32_t>
            {
                const auto blk = alloc_block(goal);
                if (!blk.has_value())
                    return std::unexpected { blk.error() };
                if (*blk == 0)
                    return std::unexpected { lib::err::no_space_left };

                if (child_meta)
                {
                    if (const auto ret = zero_block(*blk); !ret.has_value())
                        return std::unexpected { ret.error() };
                }

                ino->blocks += spb;
                finode->dirty = true;
                return *blk;
            };

            const auto ensure_top = [&](std::uint32_t which, bool child_meta)
                -> lib::expect<std::pair<std::uint32_t, bool>>
            {
                if (ino->block[which] != 0)
                    return std::make_pair(ino->block[which], false);

                const auto blk = alloc_one(child_meta);
                if (!blk.has_value())
                    return std::unexpected { blk.error() };

                ino->block[which] = *blk;
                return std::make_pair(*blk, true);
            };

            const auto ensure_slot = [&](std::uint32_t parent, std::uint32_t idx, bool child_meta)
                -> lib::expect<std::pair<std::uint32_t, bool>>
            {
                auto buf = read_at<std::uint32_t>(
                    static_cast<std::uint64_t>(parent) * block_size, ppb
                );
                if (!buf.has_value())
                    return std::unexpected { buf.error() };
                if (buf->at(idx) != 0)
                    return std::make_pair(buf->at(idx), false);

                const auto blk = alloc_one(child_meta);
                if (!blk.has_value())
                    return std::unexpected { blk.error() };
                buf->at(idx) = *blk;

                const auto ret = write_bytes(
                    static_cast<std::uint64_t>(parent) * block_size,
                    std::as_bytes(buf->span())
                );
                if (!ret.has_value())
                    return std::unexpected { ret.error() };
                return std::make_pair(*blk, true);
            };

            if (lblk < ndir_blocks)
                return ensure_top(lblk, false);
            lblk -= ndir_blocks;

            if (lblk < ppb)
            {
                const auto ind = ensure_top(ind_block, true);
                if (!ind.has_value())
                    return std::unexpected { ind.error() };
                return ensure_slot(ind->first, lblk, false);
            }
            lblk -= ppb;

            if (lblk < ppb * ppb)
            {
                const auto dind = ensure_top(dind_block, true);
                if (!dind.has_value())
                    return std::unexpected { dind.error() };
                const auto l1 = ensure_slot(dind->first, lblk / ppb, true);
                if (!l1.has_value())
                    return std::unexpected { l1.error() };
                return ensure_slot(l1->first, lblk % ppb, false);
            }
            lblk -= ppb * ppb;

            if (lblk < ppb * ppb * ppb)
            {
                const auto tind = ensure_top(tind_block, true);
                if (!tind.has_value())
                    return std::unexpected { tind.error() };
                const auto l1 = ensure_slot(tind->first, lblk / (ppb * ppb), true);
                if (!l1.has_value())
                    return std::unexpected { l1.error() };
                const auto l2 = ensure_slot(l1->first, (lblk / ppb) % ppb, true);
                if (!l2.has_value())
                    return std::unexpected { l2.error() };
                return ensure_slot(l2->first, lblk % ppb, false);
            }

            return std::unexpected { lib::err::invalid_argument };
        }

        auto instance_t::free_indirect(
            std::uint32_t blk, std::size_t level, std::uint64_t base,
            std::uint64_t from, fs_inode_t *finode
        ) -> lib::expect<bool>
        {
            const auto ino = finode->inode();
            const auto ppb = ptrs_per_block();
            const auto spb = sectors_per_block();

            auto buf = read_at<std::uint32_t>(
                static_cast<std::uint64_t>(blk) * block_size, ppb
            );
            if (!buf.has_value())
                return std::unexpected { buf.error() };

            std::uint64_t span = 1;
            for (std::size_t i = 1; i < level; i++)
                span *= ppb;

            bool modified = false;
            bool all_empty = true;

            for (std::uint64_t i = 0; i < ppb; i++)
            {
                const auto child = buf->at(i);
                const auto slot_base = base + i * span;

                if (slot_base + span <= from)
                {
                    if (child != 0)
                        all_empty = false;
                    continue;
                }
                if (child == 0)
                    continue;

                if (level == 1)
                {
                    if (const auto ret = free_block(child); !ret.has_value())
                        return std::unexpected { ret.error() };

                    ino->blocks -= std::min(ino->blocks, spb);
                    buf->at(i) = 0;
                    modified = true;
                }
                else
                {
                    const auto emptied = free_indirect(child, level - 1, slot_base, from, finode);
                    if (!emptied.has_value())
                        return std::unexpected { emptied.error() };

                    if (*emptied)
                    {
                        if (const auto ret = free_block(child); !ret.has_value())
                            return std::unexpected { ret.error() };

                        ino->blocks -= std::min(ino->blocks, spb);
                        buf->at(i) = 0;
                        modified = true;
                    }
                    else all_empty = false;
                }
            }

            if (modified)
            {
                const auto ret = write_bytes(
                    static_cast<std::uint64_t>(blk) * block_size,
                    std::as_bytes(buf->span())
                );
                if (!ret.has_value())
                    return std::unexpected { ret.error() };
            }

            return all_empty;
        }

        auto instance_t::truncate_blocks(fs_inode_t *finode, std::uint64_t new_size)
            -> lib::expect<void>
        {
            const auto ino = finode->inode();
            const auto ppb = ptrs_per_block();
            const auto spb = sectors_per_block();
            const auto from = lib::div_roundup(new_size, block_size);

            for (std::uint32_t i = 0; i < ndir_blocks; i++)
            {
                if (i >= from && ino->block[i] != 0)
                {
                    if (const auto ret = free_block(ino->block[i]); !ret.has_value())
                        return ret;

                    ino->blocks -= std::min(ino->blocks, spb);
                    ino->block[i] = 0;
                }
            }

            const auto free_tree = [&](std::uint32_t which, std::size_t level, std::uint64_t base)
                -> lib::expect<void>
            {
                if (ino->block[which] == 0)
                    return { };

                const auto ent = free_indirect(ino->block[which], level, base, from, finode);
                if (!ent.has_value())
                    return std::unexpected { ent.error() };

                if (*ent)
                {
                    if (const auto ret = free_block(ino->block[which]); !ret.has_value())
                        return ret;

                    ino->blocks -= std::min(ino->blocks, spb);
                    ino->block[which] = 0;
                }
                return { };
            };

            if (const auto ret = free_tree(ind_block, 1, ndir_blocks); !ret.has_value())
                return ret;
            if (const auto ret = free_tree(dind_block, 2, ndir_blocks + ppb); !ret.has_value())
                return ret;
            if (const auto ret = free_tree(tind_block, 3, ndir_blocks + ppb +
                    static_cast<std::uint64_t>(ppb) * ppb); !ret.has_value())
                return ret;

            if (const auto tail = new_size % block_size; tail != 0)
            {
                const auto res = bmap(ino, (new_size - 1) / block_size);
                if (!res.has_value())
                    return std::unexpected { res.error() };

                if (const std::uint64_t phys = res->first; phys != 0)
                {
                    const auto base = phys * block_size;
                    auto blk = read_at<std::byte>(base, block_size);
                    if (!blk.has_value())
                        return std::unexpected { blk.error() };

                    std::memset(blk->data() + tail, 0, block_size - tail);
                    if (const auto ret = write_bytes(base, blk->span()); !ret.has_value())
                        return ret;
                }
            }

            finode->dirty = true;
            return { };
        }

        void instance_t::set_size(fs_inode_t *finode, std::uint64_t size)
        {
            finode->stat.st_size = size;
            finode->dirty = true;
        }

        void instance_t::fold_stat_to_ino(fs_inode_t *finode)
        {
            const auto ino = finode->inode();
            const auto &stat = finode->stat;

            ino->mode = stat.st_mode;
            ino->uid = stat.st_uid;
            ino->osd2.linux2.uid_high = stat.st_uid >> 16;
            ino->gid = stat.st_gid;
            ino->osd2.linux2.gid_high = stat.st_gid >> 16;
            ino->links_count = stat.st_nlink;
            ino->atime = stat.st_atim.tv_sec;
            ino->mtime = stat.st_mtim.tv_sec;
            ino->ctime = stat.st_ctim.tv_sec;

            const std::uint64_t size = stat.st_size;
            ino->size = size;
            if (stat.type() == stat::s_ifreg)
                ino->dir_acl = size >> 32;
        }

        auto instance_t::write_inode_impl(fs_inode_t *finode) -> lib::expect<void>
        {
            fold_stat_to_ino(finode);
            if (const auto ret = write_inode_raw(finode->stat.st_ino, *finode->inode());
                !ret.has_value())
                return ret;

            finode->dirty = false;
            return { };
        }

        auto instance_t::flush_metadata() -> lib::expect<void>
        {
            superblock()->wtime = now_secs();

            if (const auto ret = write_bytes(gdt_offset(), std::as_bytes(gds.span()));
                !ret.has_value())
                return ret;
            return write_bytes(superblock_start, std::as_bytes(sb_buf.span()));
        }

        auto instance_t::collect_live() -> std::vector<std::shared_ptr<fs_inode_t>>
        {
            std::vector<std::shared_ptr<fs_inode_t>> live;

            const std::unique_lock _ { io_lock };
            live.reserve(icache.size());
            for (const auto &entry : icache)
            {
                if (auto finode = entry.second.lock())
                    live.push_back(std::move(finode));
            }
            return live;
        }

        void instance_t::flush_dirty_inodes(std::span<const std::shared_ptr<fs_inode_t>> live)
        {
            for (const auto &finode : live)
            {
                if (!finode->dirty)
                    continue;

                if (const auto ret = write_inode_impl(finode.get()); !ret.has_value())
                {
                    lib::error(
                        "ext2: could not write inode {}: {}",
                        finode->stat.st_ino, lib::error_name(ret.error())
                    );
                }
            }
        }

        void instance_t::flush_all()
        {
            const auto npsize = vmm::default_npsize();
            const auto live = collect_live();

            for (const auto &finode : live)
            {
                if (!finode->data)
                    continue;

                const auto pages = lib::div_roundup(
                    static_cast<std::uint64_t>(finode->stat.st_size), npsize
                );
                if (pages == 0)
                    continue;

                if (const auto ret = finode->data->write_back(0, pages); !ret.has_value())
                {
                    lib::error(
                        "ext2: could not write back inode {}: {}",
                        finode->stat.st_ino, lib::error_name(ret.error())
                    );
                }
            }

            const std::unique_lock _ { io_lock };
            flush_dirty_inodes(live);
            if (const auto ret = flush_metadata(); !ret.has_value())
                lib::error("ext2: could not flush metadata: {}", lib::error_name(ret.error()));
        }

        auto instance_t::free_everything(fs_inode_t *finode) -> lib::expect<void>
        {
            if (has_block_map(finode->stat.st_mode, finode->inode()))
            {
                if (const auto ret = truncate_blocks(finode, 0); !ret.has_value())
                    return ret;
            }
            return free_inode(finode->stat.st_ino, finode->stat.type() == stat::s_ifdir);
        }

        auto instance_t::dirent_lookup(fs_inode_t *dir, std::string_view name)
            -> lib::expect<std::optional<std::uint32_t>>
        {
            std::optional<std::uint32_t> found;
            const auto ret = for_each_dir_block(dir,
                [&](std::uint64_t, std::span<std::byte> data)
                {
                    return for_each_dirent(data,
                        [&](dir_entry_2_t *de, std::uint32_t) -> lib::expect<bool>
                        {
                            if (de->inode == 0 || dirent_name(de) != name)
                                return true;

                            found = de->inode;
                            return false;
                        }
                    );
                }
            );
            if (!ret.has_value())
                return std::unexpected { ret.error() };
            return found;
        }

        auto instance_t::dirent_set(
            fs_inode_t *dir, std::string_view name,
            std::uint32_t new_ino, std::uint8_t ft
        ) -> lib::expect<bool>
        {
            bool done = false;
            const auto ret = for_each_dir_block(dir,
                [&](std::uint64_t phys, std::span<std::byte> data) -> lib::expect<bool>
                {
                    const auto scanned = for_each_dirent(data,
                        [&](dir_entry_2_t *de, std::uint32_t) -> lib::expect<bool>
                        {
                            if (de->inode == 0 || dirent_name(de) != name)
                                return true;

                            de->inode = new_ino;
                            de->file_type = ft;
                            return false;
                        }
                    );
                    if (!scanned.has_value())
                        return std::unexpected { scanned.error() };
                    if (*scanned)
                        return true;

                    if (const auto w = write_bytes(phys * block_size, data); !w.has_value())
                        return std::unexpected { w.error() };

                    done = true;
                    return false;
                }
            );
            if (!ret.has_value())
                return std::unexpected { ret.error() };
            return done;
        }

        auto instance_t::dirent_insert(
            fs_inode_t *dir, std::string_view name,
            std::uint32_t new_ino, std::uint8_t ft
        ) -> lib::expect<void>
        {
            const std::uint8_t nlen = name.size();
            const auto need = dirent_reclen(nlen);

            const auto place = [&](std::byte *dst, std::uint32_t rec_len)
            {
                const auto ne = reinterpret_cast<dir_entry_2_t *>(dst);
                ne->inode = new_ino;
                ne->rec_len = rec_len;
                ne->name_len = nlen;
                ne->file_type = ft;
                std::memcpy(ne->name, name.data(), nlen);
            };

            bool placed = false;
            const auto ret = for_each_dir_block(dir,
                [&](std::uint64_t phys, std::span<std::byte> data) -> lib::expect<bool>
                {
                    const auto scanned = for_each_dirent(data,
                        [&](dir_entry_2_t *de, std::uint32_t i) -> lib::expect<bool>
                        {
                            const std::uint32_t rec = de->rec_len;
                            const auto used = de->inode == 0 ? 0u : dirent_reclen(de->name_len);
                            if (rec - used < need)
                                return true;

                            if (de->inode != 0)
                            {
                                de->rec_len = used;
                                place(data.data() + i + used, rec - used);
                            }
                            else place(data.data() + i, rec);
                            return false;
                        }
                    );
                    if (!scanned.has_value())
                        return std::unexpected { scanned.error() };
                    if (*scanned)
                        return true;

                    if (const auto w = write_bytes(phys * block_size, data); !w.has_value())
                        return std::unexpected { w.error() };

                    placed = true;
                    return false;
                }
            );
            if (!ret.has_value())
                return ret;
            if (placed)
                return { };

            const std::uint64_t size = dir->stat.st_size;
            const auto pr = bmap_alloc(dir, size / block_size);
            if (!pr.has_value())
                return std::unexpected { pr.error() };

            lib::membuffer blk { block_size, lib::zeroed };
            place(blk.data(), block_size);

            const auto res = write_bytes(
                static_cast<std::uint64_t>(pr->first) * block_size,
                blk.span()
            );
            if (!res.has_value())
                return res;

            set_size(dir, size + block_size);
            dir->stat.st_blocks = dir->inode()->blocks;
            return { };
        }

        auto instance_t::dirent_remove(fs_inode_t *dir, std::string_view name)
            -> lib::expect<void>
        {
            bool removed = false;
            const auto ret = for_each_dir_block(dir,
                [&](std::uint64_t phys, std::span<std::byte> data) -> lib::expect<bool>
                {
                    dir_entry_2_t *prev = nullptr;
                    const auto scanned = for_each_dirent(data,
                        [&](dir_entry_2_t *de, std::uint32_t) -> lib::expect<bool>
                        {
                            if (de->inode == 0 || dirent_name(de) != name)
                            {
                                prev = de;
                                return true;
                            }

                            if (prev != nullptr)
                            {
                                prev->rec_len += de->rec_len;
                            }
                            else de->inode = 0;

                            return false;
                        }
                    );
                    if (!scanned.has_value())
                        return std::unexpected { scanned.error() };
                    if (*scanned)
                        return true;

                    if (const auto w = write_bytes(phys * block_size, data); !w.has_value())
                        return std::unexpected { w.error() };

                    removed = true;
                    return false;
                }
            );
            if (!ret.has_value())
                return ret;
            if (!removed)
                return std::unexpected { lib::err::not_found };
            return { };
        }

        auto instance_t::create(
            std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
            mode_t mode, dev_t rdev, std::optional<std::shared_ptr<vfs::ops_t>> ops
        ) -> lib::expect<std::shared_ptr<vfs::inode_t>>
        {
            if (read_only())
                return std::unexpected { lib::err::read_only_fs };

            const std::unique_lock _ { io_lock };

            const auto pfi = inode_of(parent);
            const auto type = stat::type(mode);
            const bool is_dir = type == stat::s_ifdir;
            const auto goal = (pfi->stat.st_ino - 1) / superblock()->inodes_per_group;

            const auto ino_res = alloc_inode(goal, is_dir);
            if (!ino_res.has_value())
                return std::unexpected { ino_res.error() };
            if (*ino_res == 0)
                return std::unexpected { lib::err::no_space_left };
            const auto ino_num = *ino_res;

            lib::buffer<ext2::inode_t> ibuf { 1, lib::zeroed };
            const auto di = ibuf.data();
            di->mode = static_cast<std::uint16_t>(mode);

            const auto proc = sched::current_process();
            const uid_t uid = proc->cred->euid;
            const gid_t gid = proc->cred->egid;

            di->uid = uid;
            di->osd2.linux2.uid_high = uid >> 16;
            di->gid = gid;
            di->osd2.linux2.gid_high = gid >> 16;

            di->links_count = is_dir ? 2 : 1;

            const auto now = now_secs();
            di->atime = di->ctime = di->mtime = now;
            if (type == stat::s_ifchr || type == stat::s_ifblk)
                encode_dev(rdev, di->block);

            auto finode = fs_inode_t::create(this, pfi->handle, std::move(ibuf), ino_num, ops);

            bool linked_parent = false;
            const auto build = [&]() -> lib::expect<void>
            {
                if (is_dir)
                {
                    const auto pr = bmap_alloc(finode.get(), 0);
                    if (!pr.has_value())
                        return std::unexpected { pr.error() };

                    lib::membuffer blk { block_size, lib::zeroed };

                    const auto dot = reinterpret_cast<dir_entry_2_t *>(blk.data());
                    dot->inode = ino_num;
                    dot->rec_len = dirent_reclen(1);
                    dot->name_len = 1;
                    dot->file_type = ft_dir;
                    dot->name[0] = '.';

                    const auto dotdot = reinterpret_cast<dir_entry_2_t *>(
                        blk.data() + dot->rec_len
                    );
                    dotdot->inode = pfi->stat.st_ino;
                    dotdot->rec_len = block_size - dot->rec_len;
                    dotdot->name_len = 2;
                    dotdot->file_type = ft_dir;
                    dotdot->name[0] = '.';
                    dotdot->name[1] = '.';

                    if (const auto ret = write_bytes(
                            static_cast<std::uint64_t>(pr->first) * block_size, blk.span()
                        ); !ret.has_value())
                        return ret;

                    set_size(finode.get(), block_size);
                    finode->stat.st_blocks = finode->inode()->blocks;

                    pfi->stat.st_nlink++;
                    pfi->dirty = true;
                    linked_parent = true;
                }

                if (const auto ret = write_inode_impl(finode.get()); !ret.has_value())
                    return ret;
                if (const auto ret = dirent_insert(pfi, name, ino_num, get_filetype(mode));
                    !ret.has_value())
                    return ret;

                pfi->stat.update_time(kstat::time::modify | kstat::time::status);
                return write_inode_impl(pfi);
            };

            if (const auto ret = build(); !ret.has_value())
            {
                if (linked_parent)
                {
                    pfi->stat.st_nlink--;
                    pfi->dirty = true;
                }
                if (const auto undo = free_everything(finode.get()); !undo.has_value())
                {
                    lib::error(
                        "ext2: could not roll back inode {}: {}",
                        ino_num, lib::error_name(undo.error())
                    );
                }
                return std::unexpected { ret.error() };
            }

            icache.insert_or_assign(ino_num, finode);
            return std::static_pointer_cast<vfs::inode_t>(std::move(finode));
        }

        auto instance_t::symlink(
            std::shared_ptr<vfs::inode_t> &parent, std::string_view name, lib::path target
        ) -> lib::expect<std::shared_ptr<vfs::inode_t>>
        {
            if (read_only())
                return std::unexpected { lib::err::read_only_fs };

            const std::unique_lock _ { io_lock };

            const auto pfi = inode_of(parent);
            const auto tstr = target.str();
            const auto goal = (pfi->stat.st_ino - 1) / superblock()->inodes_per_group;

            const auto ino_res = alloc_inode(goal, false);
            if (!ino_res.has_value())
                return std::unexpected { ino_res.error() };
            if (*ino_res == 0)
                return std::unexpected { lib::err::no_space_left };
            const auto ino_num = *ino_res;

            lib::buffer<ext2::inode_t> ibuf { 1, lib::zeroed };
            const auto di = ibuf.data();
            di->mode = stat::s_iflnk | 0777;

            const auto proc = sched::current_process();
            di->uid = proc->cred->euid;
            di->gid = proc->cred->egid;
            di->links_count = 1;

            const auto now = now_secs();
            di->atime = di->ctime = di->mtime = now;

            auto finode = fs_inode_t::create(this, pfi->handle, std::move(ibuf), ino_num);

            const auto build = [&]() -> lib::expect<void>
            {
                if (tstr.size() < fast_symlink_size)
                    std::memcpy(finode->inode()->block, tstr.data(), tstr.size());
                else
                {
                    const auto pr = bmap_alloc(finode.get(), 0);
                    if (!pr.has_value())
                        return std::unexpected { pr.error() };

                    lib::membuffer blk { block_size, lib::zeroed };
                    std::memcpy(blk.data(), tstr.data(), tstr.size());

                    if (const auto ret = write_bytes(
                            static_cast<std::uint64_t>(pr->first) * block_size, blk.span()
                        ); !ret.has_value())
                        return ret;

                    finode->stat.st_blocks = finode->inode()->blocks;
                }

                set_size(finode.get(), tstr.size());

                if (const auto ret = write_inode_impl(finode.get()); !ret.has_value())
                    return ret;
                if (const auto ret = dirent_insert(pfi, name, ino_num, ft_symlink); !ret.has_value())
                    return ret;

                pfi->stat.update_time(kstat::time::modify | kstat::time::status);
                return write_inode_impl(pfi);
            };

            if (const auto ret = build(); !ret.has_value())
            {
                if (const auto undo = free_everything(finode.get()); !undo.has_value())
                {
                    lib::error(
                        "ext2: could not roll back inode {}: {}",
                        ino_num, lib::error_name(undo.error())
                    );
                }
                return std::unexpected { ret.error() };
            }

            icache.insert_or_assign(ino_num, finode);
            return std::static_pointer_cast<vfs::inode_t>(std::move(finode));
        }

        auto instance_t::link(
            std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
            std::shared_ptr<vfs::inode_t> target
        ) -> lib::expect<std::shared_ptr<vfs::inode_t>>
        {
            if (read_only())
                return std::unexpected { lib::err::read_only_fs };

            const std::unique_lock _ { io_lock };

            const auto pfi = inode_of(parent);
            const auto tfi = inode_of(target);

            if (const auto ret = dirent_insert(
                    pfi, name, tfi->stat.st_ino, get_filetype(tfi->stat.st_mode)
                ); !ret.has_value())
                return std::unexpected { ret.error() };

            tfi->stat.st_nlink++;
            tfi->stat.update_time(kstat::time::status);
            tfi->dirty = true;

            pfi->stat.update_time(kstat::time::modify | kstat::time::status);
            pfi->dirty = true;

            if (const auto ret = write_inode_impl(tfi); !ret.has_value())
                return std::unexpected { ret.error() };
            if (const auto ret = write_inode_impl(pfi); !ret.has_value())
                return std::unexpected { ret.error() };
            return target;
        }

        auto instance_t::unlink(
            std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
            std::shared_ptr<vfs::inode_t> &inode
        ) -> lib::expect<void>
        {
            if (read_only())
                return std::unexpected { lib::err::read_only_fs };

            const std::unique_lock _ { io_lock };

            const auto pfi = inode_of(parent);
            const auto fi = inode_of(inode);

            if (const auto ret = dirent_remove(pfi, name); !ret.has_value())
                return ret;

            if (fi->stat.type() == stat::s_ifdir)
            {
                pfi->stat.st_nlink--;
                fi->stat.st_nlink = 0;
            }
            else if (fi->stat.st_nlink > 0)
                fi->stat.st_nlink--;

            pfi->stat.update_time(kstat::time::modify | kstat::time::status);
            pfi->dirty = true;

            fi->stat.update_time(kstat::time::status);
            fi->dirty = true;

            if (fi->stat.st_nlink == 0)
            {
                fi->inode()->dtime = now_secs();
                fi->destroy = true;
            }

            if (const auto ret = write_inode_impl(fi); !ret.has_value())
                return ret;
            return write_inode_impl(pfi);
        }

        auto instance_t::rename(
            std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
            std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
            std::shared_ptr<vfs::inode_t> replaced
        ) -> lib::expect<void>
        {
            if (read_only())
                return std::unexpected { lib::err::read_only_fs };

            const std::unique_lock _ { io_lock };

            const auto opfi = inode_of(old_parent);
            const auto npfi = inode_of(new_parent);

            const auto mv = dirent_lookup(opfi, old_name);
            if (!mv.has_value())
                return std::unexpected { mv.error() };
            if (!mv->has_value())
                return std::unexpected { lib::err::not_found };

            const auto moving = iget(opfi->handle, **mv);
            if (!moving.has_value())
                return std::unexpected { moving.error() };
            const auto mfi = moving->get();

            const bool is_dir = mfi->stat.type() == stat::s_ifdir;
            const auto ft = get_filetype(mfi->stat.st_mode);

            if (replaced)
            {
                const auto set = dirent_set(npfi, new_name, mfi->stat.st_ino, ft);
                if (!set.has_value())
                    return std::unexpected { set.error() };
                if (!*set)
                    return std::unexpected { lib::err::not_found };
            }
            else if (const auto ret = dirent_insert(npfi, new_name, mfi->stat.st_ino, ft);
                !ret.has_value())
                return ret;

            if (const auto ret = dirent_remove(opfi, old_name); !ret.has_value())
                return ret;

            if (is_dir && opfi != npfi)
            {
                if (const auto ret = dirent_set(mfi, "..", npfi->stat.st_ino, ft_dir);
                    !ret.has_value())
                    return std::unexpected { ret.error() };

                opfi->stat.st_nlink--;
                npfi->stat.st_nlink++;
            }

            if (replaced)
            {
                const auto rfi = inode_of(replaced);
                if (rfi->stat.type() == stat::s_ifdir)
                {
                    npfi->stat.st_nlink--;
                    rfi->stat.st_nlink = 0;
                }
                else if (rfi->stat.st_nlink > 0)
                    rfi->stat.st_nlink--;

                rfi->stat.update_time(kstat::time::status);
                rfi->dirty = true;

                if (rfi->stat.st_nlink == 0)
                {
                    rfi->inode()->dtime = now_secs();
                    rfi->destroy = true;
                }

                if (const auto ret = write_inode_impl(rfi); !ret.has_value())
                    return ret;
            }

            mfi->stat.update_time(kstat::time::status);
            mfi->dirty = true;

            opfi->stat.update_time(kstat::time::modify | kstat::time::status);
            opfi->dirty = true;

            if (const auto ret = write_inode_impl(mfi); !ret.has_value())
                return ret;
            if (const auto ret = write_inode_impl(opfi); !ret.has_value())
                return ret;

            if (opfi != npfi)
            {
                npfi->stat.update_time(kstat::time::modify | kstat::time::status);
                npfi->dirty = true;
                if (const auto ret = write_inode_impl(npfi); !ret.has_value())
                    return ret;
            }
            return { };
        }

        auto instance_t::write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void>
        {
            if (read_only())
                return { };

            const std::unique_lock _ { io_lock };
            return write_inode_impl(inode_of(inode));
        }

        auto instance_t::dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void>
        {
            const std::unique_lock _ { io_lock };
            inode->dirty = true;
            return { };
        }

        bool instance_t::sync()
        {
            if (!read_only())
            {
                flush_all();
                lib::unused(src->sync());
            }
            return true;
        }

        bool instance_t::unmount(std::shared_ptr<vfs::mount_t> mnt)
        {
            lib::unused(mnt);
            if (!read_only())
            {
                mark_clean();
                flush_all();
                lib::unused(src->sync());
            }
            return true;
        }

        bool instance_t::remount(std::uint64_t new_flags)
        {
            const bool was_rw = !read_only();
            const bool now_rw = !(new_flags & vfs::ms_rdonly);

            if (!was_rw && now_rw && !check_features(superblock(), true).has_value())
                return false;

            if (was_rw && !now_rw)
            {
                mark_clean();
                flush_all();
                lib::unused(src->sync());
            }

            flags = new_flags;

            if (!was_rw && now_rw)
            {
                {
                    const std::unique_lock _ { io_lock };
                    auto sb = superblock();
                    sb->state &= ~state_clean;
                    sb->mnt_count++;
                    sb->mtime = now_secs();
                }
                flush_all();
                lib::unused(src->sync());
            }
            return true;
        }

        fs_inode_t::~fs_inode_t()
        {
            const std::unique_lock _ { owner->io_lock };

            if (const auto it = owner->icache.find(stat.st_ino);
                it != owner->icache.end() && it->second.expired())
                owner->icache.erase(it);

            if (!destroy)
                return;

            if (const auto ret = owner->free_everything(this); !ret.has_value())
            {
                lib::error(
                    "ext2: could not free inode {}: {}",
                    stat.st_ino, lib::error_name(ret.error())
                );
            }
            lib::unused(owner->flush_metadata());
        }
    } // namespace

    struct fs_t : vfs::filesystem_t
    {
        auto mount(
            std::shared_ptr<vfs::dentry_t> src, std::uint64_t flags,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
        {
            // TODO
            lib::unused(data);

            const bool rw = !(flags & vfs::ms_rdonly);

            auto file = vfs::file_t::create({ nullptr, src }, 0, 0);
            if (const auto ret = file->open(0, sched::current_process()->pid); !ret.has_value())
                return std::unexpected { ret.error() };

            auto sbres = file->read_obj<superblock_t>(superblock_start);
            if (!sbres.has_value())
                return std::unexpected { sbres.error() };

            auto sbuf = std::move(*sbres);
            const auto *sb = sbuf.data();

            if (sb->magic != magic)
                return std::unexpected { lib::err::invalid_argument };

            if (const auto ret = check_features(sb, rw); !ret.has_value())
                return std::unexpected { ret.error() };

            if (sb->log_block_size > max_log_block_size)
                return std::unexpected { lib::err::invalid_argument };

            const std::uint32_t block_size = 1024u << sb->log_block_size;

            if (sb->first_data_block != (block_size == superblock_start ? 1u : 0u))
                return std::unexpected { lib::err::invalid_argument };

            if (sb->blocks_per_group == 0 || sb->inodes_per_group == 0)
                return std::unexpected { lib::err::invalid_argument };

            if (sb->blocks_per_group > block_size * 8 || sb->inodes_per_group > block_size * 8)
                return std::unexpected { lib::err::invalid_argument };

            if (sb->inodes_count == 0 || sb->blocks_count <= sb->first_data_block)
                return std::unexpected { lib::err::invalid_argument };

            const std::uint16_t inode_size = sb->rev_level >= 1 ? sb->inode_size : 128;
            if (inode_size < sizeof(inode_t) || inode_size > block_size ||
                (inode_size & (inode_size - 1)) != 0)
                return std::unexpected { lib::err::invalid_argument };

            const auto group_count = lib::div_roundup(
                sb->blocks_count - sb->first_data_block,
                sb->blocks_per_group
            );

            auto gdres = file->read_obj<group_desc_t>(
                static_cast<std::uint64_t>(sb->first_data_block + 1) * block_size, group_count
            );
            if (!gdres.has_value())
                return std::unexpected { gdres.error() };

            std::shared_ptr<vfs::dentry_t> root;

            auto instance = lib::make_locked<ext2::instance_t, sched::mutex_t>(
                std::move(file), std::move(sbuf), std::move(*gdres),
                block_size, inode_size, flags
            );
            {
                auto locked = instance.lock();
                locked->fs = const_cast<fs_t *>(this);

                auto rres = locked->iget(instance, root_ino);
                if (!rres.has_value())
                    return std::unexpected { rres.error() };

                root = std::make_shared<vfs::dentry_t>();
                root->name = "ext2 root";
                root->inode = std::move(*rres);
                root->parent = root;

                if (rw)
                {
                    auto msb = locked->superblock();
                    msb->state &= ~state_clean;
                    msb->mnt_count++;
                    msb->mtime = now_secs();
                    if (const auto ret = locked->flush_metadata(); !ret.has_value())
                        return std::unexpected { ret.error() };
                }
            }

            return std::make_shared<struct vfs::mount_t>(std::move(instance), root);
        }

        fs_t() : vfs::filesystem_t { "ext2", ext2::magic, true } { }
    } filesystem;
} // namespace ext2

filesystem_module(
    "ext2", "Second Extended Filesystem",
    ext2::filesystem
);
