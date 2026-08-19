// Copyright (C) 2024-2026  ilobilo

import system.memory.virt;
import system.sched;
import system.vfs;
import magic_enum;
import lib;
import std;

import squashfs;

namespace squashfs
{
    using namespace magic_enum::bitwise_operators;

    namespace
    {
        constexpr std::size_t mcache_limit = 32;
        constexpr std::size_t icache_clean = 256;
        constexpr std::size_t max_readdir_batch = 256;

        struct metadata_cursor_t
        {
            std::uint64_t block = 0;
            std::size_t offset = 0;
            std::uint64_t limit = -1;
        };

        struct metadata_block_t
        {
            std::uint64_t next = 0;
            bool uncompressed = false;
            lib::membuffer data;
        };

        struct metadata_entry_t
        {
            std::uint64_t location;
            std::shared_ptr<metadata_block_t> block;
        };

        struct instance_t;
        using instance_ptr = lib::locked_ptr<instance_t, sched::mutex_t>;

        struct fs_inode_t;
        struct instance_t : vfs::filesystem_t::instance_t
        {
            std::shared_ptr<vfs::file_t> src;
            lib::buffer<superblock_t> sb_buf;
            lib::decompressor decompressor;

            std::uint64_t dir_table_end;

            lib::buffer<std::uint32_t> ids;

            lib::list<metadata_entry_t> metadata;
            lib::map::flat_hash<
                std::uint64_t,
                decltype(metadata)::iterator
            > mcache;

            lib::map::flat_hash<
                std::uint64_t,
                std::weak_ptr<fs_inode_t>
            > icache;

            instance_t(
                std::shared_ptr<vfs::file_t> src, lib::buffer<superblock_t> sb,
                lib::decompressor decomp
            ) : src { std::move(src) }, sb_buf { std::move(sb) },
                decompressor { std::move(decomp) }, dir_table_end { 0 }, ids { }, metadata { },
                mcache { }, icache { } { }

            auto superblock(this auto &&self) { return self.sb_buf.data(); }

            template<typename Type = std::byte>
            lib::expect<lib::buffer<Type>> read_obj(std::uint64_t offset, std::size_t count = 1)
            {
                const auto limit = superblock()->bytes_used;
                if (offset > limit || count > (limit - offset) / sizeof(Type))
                    return std::unexpected { lib::err::corrupted_data };
                return src->read_obj<Type>(offset, count);
            }

            auto read_metadata_block(std::uint64_t location)
                -> lib::expect<std::shared_ptr<metadata_block_t>>
            {
                if (const auto it = mcache.find(location); it != mcache.end())
                {
                    metadata.move_to_front(it->second);
                    return it->second->block;
                }

                const auto *sb = superblock();
                if (location > sb->bytes_used - sizeof(std::uint16_t))
                    return std::unexpected { lib::err::corrupted_data };

                auto hdr_buf = read_obj<std::uint16_t>(location);
                if (!hdr_buf)
                    return std::unexpected { hdr_buf.error() };
                const auto hdr = **hdr_buf;

                const std::size_t size = hdr & metadata_size_mask;
                const bool uncompressed = hdr & metadata_uncompressed;

                if (size == 0 || size > metadata_size)
                    return std::unexpected { lib::err::corrupted_data };

                if (size > sb->bytes_used - location - sizeof(hdr))
                    return std::unexpected { lib::err::corrupted_data };

                auto input = read_obj(location + sizeof(hdr), size);
                if (!input)
                    return std::unexpected { input.error() };

                lib::membuffer output;
                if (!uncompressed)
                {
                    output.allocate(metadata_size);
                    auto res = decompressor(input->span(), output.span());
                    if (!res)
                        return std::unexpected { res.error() };
                    if (*res == 0)
                        return std::unexpected { lib::err::corrupted_data };

                    lib::membuffer buffer { *res };
                    std::memcpy(buffer.data(), output.data(), *res);
                    output = std::move(buffer);
                }
                else output = std::move(*input);

                auto block = std::make_shared<metadata_block_t>();
                block->next = location + sizeof(hdr) + size;
                block->uncompressed = uncompressed;
                block->data = std::move(output);

                if (metadata.size() >= mcache_limit)
                {
                    mcache.erase(metadata.back().location);
                    metadata.pop_back();
                }

                mcache[location] = metadata.push_front({ location, block });
                return block;
            }

            lib::expect<void> read_metadata(metadata_cursor_t &cursor, std::span<std::byte> out)
            {
                while (!out.empty())
                {
                    if (cursor.block >= cursor.limit)
                        return std::unexpected { lib::err::corrupted_data };

                    auto res = read_metadata_block(cursor.block);
                    if (!res)
                        return std::unexpected { res.error() };

                    const auto &block = **res;
                    if (block.next > cursor.limit || cursor.offset > block.data.size())
                        return std::unexpected { lib::err::corrupted_data };

                    if (cursor.offset == block.data.size())
                    {
                        cursor.block = block.next;
                        cursor.offset = 0;
                        continue;
                    }

                    const auto num = std::min(out.size(), block.data.size() - cursor.offset);
                    std::memcpy(out.data(), block.data.data() + cursor.offset, num);

                    cursor.offset += num;
                    out = out.subspan(num);
                }
                return { };
            }

            template<typename Type>
                requires std::is_trivially_copyable_v<Type>
            lib::expect<Type> read_metadata(metadata_cursor_t &cursor)
            {
                Type value { };
                auto bytes = std::as_writable_bytes(std::span<Type> { &value, 1 });
                if (const auto ret = read_metadata(cursor, bytes); !ret)
                    return std::unexpected { ret.error() };
                return value;
            }

            lib::expect<void> skip_metadata(metadata_cursor_t &cursor, std::size_t size)
            {
                while (size != 0)
                {
                    if (cursor.block >= cursor.limit)
                        return std::unexpected { lib::err::corrupted_data };

                    auto res = read_metadata_block(cursor.block);
                    if (!res)
                        return std::unexpected { res.error() };

                    const auto &block = **res;
                    if (block.next > cursor.limit || cursor.offset > block.data.size())
                        return std::unexpected { lib::err::corrupted_data };

                    if (cursor.offset == block.data.size())
                    {
                        cursor.block = block.next;
                        cursor.offset = 0;
                        continue;
                    }

                    const auto num = std::min(size, block.data.size() - cursor.offset);
                    cursor.offset += num;
                    size -= num;
                }
                return { };
            }

            lib::expect<std::uint64_t> table_location(
                std::uint64_t table, std::uint64_t limit, std::uint64_t relative
            )
            {
                if (limit > superblock()->bytes_used || table >= limit || relative >= limit - table)
                    return std::unexpected { lib::err::corrupted_data };
                return table + relative;
            }

            lib::expect<void> initialise()
            {
                const auto *sb = superblock();
                const bool has_options = (sb->flags & flag::has_compress_options) != flag::none;

                if (!has_options && sb->compressor == compressor::lz4)
                    return std::unexpected { lib::err::corrupted_data };

                if (has_options)
                {
                    auto block = read_metadata_block(sizeof(superblock_t));
                    if (!block)
                        return std::unexpected { block.error() };

                    if (!(*block)->uncompressed || (*block)->next > sb->inode_table)
                        return std::unexpected { lib::err::corrupted_data };

                    if (sb->compressor == compressor::lz4)
                    {
                        if ((*block)->data.size() < sizeof(lz4_t))
                            return std::unexpected { lib::err::corrupted_data };

                        metadata_cursor_t cursor {
                            .block = sizeof(superblock_t),
                            .offset = 0,
                            .limit = sb->inode_table
                        };

                        auto options = read_metadata<lz4_t>(cursor);
                        if (!options)
                            return std::unexpected { options.error() };

                        if (options->version != 1)
                            return std::unexpected { lib::err::invalid_argument };
                    }
                }

                const std::size_t num_ids = sb->ids;
                const auto blocks = lib::div_roundup(
                    num_ids * sizeof(std::uint32_t), metadata_size
                );

                auto locs = read_obj<std::uint64_t>(sb->id_table, blocks);
                if (!locs)
                    return std::unexpected { locs.error() };

                for (std::size_t i = 0; i < blocks; i++)
                {
                    const auto location = (*locs)[i];
                    if (location < sizeof(superblock_t) || location >= sb->id_table)
                        return std::unexpected { lib::err::corrupted_data };

                    if (i != 0)
                    {
                        const auto previous = (*locs)[i - 1];
                        if (location <= previous ||
                            location - previous > metadata_size + sizeof(std::uint16_t))
                            return std::unexpected { lib::err::corrupted_data };
                    }
                }

                if (sb->id_table - locs->back() > metadata_size + sizeof(std::uint16_t))
                    return std::unexpected { lib::err::corrupted_data };

                if (locs->front() < sb->dir_table)
                    return std::unexpected { lib::err::corrupted_data };
                dir_table_end = locs->front();

                const auto check_table = [&](std::uint64_t table) -> lib::expect<void> {
                    auto first = read_obj<std::uint64_t>(table);
                    if (!first)
                        return std::unexpected { first.error() };

                    const auto location = **first;
                    if (location < sb->dir_table || location >= table)
                        return std::unexpected { lib::err::corrupted_data };

                    dir_table_end = std::min(dir_table_end, location);
                    return { };
                };

                if (sb->frags != 0)
                {
                    if (const auto ret = check_table(sb->frag_table); !ret)
                        return ret;
                }

                if ((sb->flags & flag::has_export_table) != flag::none)
                {
                    if (const auto ret = check_table(sb->export_table); !ret)
                        return ret;
                }

                ids.allocate(num_ids);
                for (std::size_t i = 0; i < num_ids; i++)
                {
                    const auto offset = i * sizeof(std::uint32_t);
                    metadata_cursor_t cursor {
                        .block = (*locs)[offset / metadata_size],
                        .offset = offset % metadata_size,
                        .limit = sb->id_table
                    };

                    auto id = read_metadata<std::uint32_t>(cursor);
                    if (!id)
                        return std::unexpected { id.error() };
                    ids[i] = *id;
                }
                return { };
            }

            auto iget(
                const instance_ptr &handle, std::uint64_t reference,
                std::optional<std::uint32_t> expected_ino = std::nullopt,
                std::optional<inode_type> expected_type = std::nullopt
            ) -> lib::expect<std::shared_ptr<fs_inode_t>>;

            auto create(
                std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                mode_t mode, dev_t rdev, std::optional<std::shared_ptr<vfs::ops_t>> ops
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                lib::unused(parent, name, mode, rdev, ops);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto symlink(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto link(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, std::shared_ptr<vfs::inode_t> target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto unlink(
                std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                std::shared_ptr<vfs::inode_t> &inode
            ) -> lib::expect<void> override
            {
                lib::unused(parent, name, inode);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto rename(
                std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode_t> replaced
            ) -> lib::expect<void> override
            {
                lib::unused(old_parent, old_name, new_parent, new_name, replaced);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override;

            auto lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
                -> lib::expect<vfs::dir_entry> override;

            auto readlink(std::shared_ptr<vfs::dentry_t> dentry) -> lib::expect<lib::path> override;

            void statfs(struct ::statfs &out) override
            {
                vfs::filesystem_t::instance_t::statfs(out);

                const auto sb = superblock();
                out.f_bsize = sb->block_size;
                out.f_frsize = sb->block_size;
                out.f_blocks = 1 + (sb->bytes_used - 1) / sb->block_size;
                out.f_bfree = 0;
                out.f_bavail = 0;
                out.f_files = sb->inodes;
                out.f_ffree = 0;
                out.f_namelen = max_namelen;
            }

            auto write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                lib::unused(inode);
                return { };
            }

            auto dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                lib::unused(inode);
                return { };
            }

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
                lib::unused(inode, name, data, flags);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto remxattr(std::shared_ptr<vfs::inode_t> &inode, std::string_view name)
                -> lib::expect<void> override
            {
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

            bool sync() override { return true; }
            bool unmount(std::shared_ptr<vfs::mount_t> mnt) override
            {
                lib::unused(mnt);
                return true;
            }
        };

        struct ops_t : vfs::ops_t
        {
            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override;

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                lib::unused(file, offset, buffer);
                return std::unexpected { lib::err::read_only_fs };
            }

            lib::expect<void> trunc(std::shared_ptr<vfs::file_t> file, std::size_t size) override
            {
                lib::unused(file, size);
                return std::unexpected { lib::err::read_only_fs };
            }

            // TODO
            // lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file_t> file) override;

            lib::expect<void> sync(std::shared_ptr<vfs::file_t> file, bool datasync) override
            {
                lib::unused(file, datasync);
                return { };
            }

            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }
        };

        struct fs_inode_t : vfs::inode_t
        {
            instance_ptr handle;
            inode_type type;

            metadata_cursor_t symlink_cursor;
            metadata_cursor_t dir_cursor;
            metadata_cursor_t dir_index_cursor;
            metadata_cursor_t block_list_cursor;

            std::uint16_t dir_index_count;

            std::uint64_t block_start;
            std::uint32_t frag;
            std::uint32_t frag_offset;
            std::uint64_t sparse;

            fs_inode_t(
                instance_ptr handle, inode_type type, std::shared_ptr<vfs::ops_t> ops
            ) : vfs::inode_t { std::move(ops) }, handle { std::move(handle) }, type { type },
                symlink_cursor { }, dir_cursor { }, dir_index_cursor { }, block_list_cursor { },
                dir_index_count { 0 }, block_start { 0 }, frag { 0 }, frag_offset { 0 },
                sparse { 0 } { }
        };

        fs_inode_t *get_inode(const std::shared_ptr<vfs::dentry_t> &dentry)
        {
            lib::bug_on(!dentry || !dentry->inode);
            return static_cast<fs_inode_t *>(dentry->inode.get());
        }

        struct dir_pos_t
        {
            metadata_cursor_t cursor;
            std::uint64_t offset;
        };

        auto find_dir_pos(
            instance_t &fs, fs_inode_t *dir, std::uint64_t target_offset,
            std::optional<std::string_view> target_name
        ) -> lib::expect<dir_pos_t>
        {
            dir_pos_t result { dir->dir_cursor, 0 };
            if (dir->dir_index_count == 0)
                return result;

            const std::uint64_t data_size = dir->stat.st_size - 3;
            auto cursor = dir->dir_index_cursor;

            std::array<char, max_namelen> name_buffer;
            std::uint32_t prev_offset = 0;

            for (std::size_t i = 0; i < dir->dir_index_count; i++)
            {
                auto raw = fs.read_metadata<dir_index_t>(cursor);
                if (!raw)
                    return std::unexpected { raw.error() };

                if (raw->name_size >= max_namelen || raw->index >= data_size ||
                    (i != 0 && raw->index < prev_offset))
                    return std::unexpected { lib::err::corrupted_data };
                prev_offset = raw->index;

                const std::size_t name_size = raw->name_size + 1;
                auto span = std::span { name_buffer } .first(name_size);
                if (const auto ret = fs.read_metadata(cursor, std::as_writable_bytes(span)); !ret)
                    return std::unexpected { ret.error() };

                const std::string_view index_name { name_buffer.data(), name_size };
                if (index_name.contains('\0') || index_name.contains('/'))
                    return std::unexpected { lib::err::corrupted_data };

                const bool usable = target_name
                    ? index_name <= *target_name
                    : raw->index <= target_offset;
                if (!usable)
                    break;

                const auto *sb = fs.superblock();
                auto location = fs.table_location(sb->dir_table, fs.dir_table_end, raw->start);
                if (!location)
                    return std::unexpected { location.error() };

                result = {
                    .cursor = {
                        .block = *location,
                        .offset = (dir->dir_cursor.offset + raw->index) % metadata_size,
                        .limit = fs.dir_table_end
                    },
                    .offset = raw->index
                };
            }
            return result;
        }

        auto walk_dir(
            instance_t &fs, fs_inode_t *dir, std::size_t cookie,
            std::optional<std::string_view> target_name, auto &&fn
        ) -> lib::expect<void>
        {
            if (dir->stat.type() != stat::s_ifdir)
                return std::unexpected { lib::err::not_a_dir };

            const std::uint64_t data_size = dir->stat.st_size - 3;
            const std::uint64_t target_offset = cookie <= 3 ? 0 : cookie - 3;
            if (target_offset >= data_size)
                return { };

            auto pos = find_dir_pos(fs, dir, target_offset, target_name);
            if (!pos)
                return std::unexpected { pos.error() };

            auto cursor = pos->cursor;
            auto position = pos->offset;
            std::array<char, max_namelen> name_buffer;

            while (position < data_size)
            {
                if (data_size - position < sizeof(dir_hdr_t))
                    return std::unexpected { lib::err::corrupted_data };

                auto header = fs.read_metadata<dir_hdr_t>(cursor);
                if (!header)
                    return std::unexpected { header.error() };
                position += sizeof(dir_hdr_t);

                if (header->count >= max_dir_count)
                    return std::unexpected { lib::err::corrupted_data };

                const std::size_t count = header->count + 1;
                for (std::size_t i = 0; i < count; i++)
                {
                    if (data_size - position < sizeof(dir_entry_t))
                        return std::unexpected { lib::err::corrupted_data };

                    const auto entry_cookie = position + 3;
                    auto entry = fs.read_metadata<dir_entry_t>(cursor);
                    if (!entry)
                        return std::unexpected { entry.error() };
                    position += sizeof(dir_entry_t);

                    if (entry->offset >= metadata_size || entry->type < inode_type::basic_dir ||
                        entry->type > inode_type::basic_sock || entry->name_size >= max_namelen)
                        return std::unexpected { lib::err::corrupted_data };

                    const std::size_t name_size = entry->name_size + 1;
                    if (name_size > data_size - position)
                        return std::unexpected { lib::err::corrupted_data };

                    auto span = std::span { name_buffer } .first(name_size);
                    if (const auto ret = fs.read_metadata(cursor, std::as_writable_bytes(span)); !ret)
                        return std::unexpected { ret.error() };
                    position += name_size;

                    const std::string_view name { name_buffer.data(), name_size };
                    if (name == "." || name == ".." || name.contains('\0') || name.contains('/'))
                        return std::unexpected { lib::err::corrupted_data };

                    const auto ino = static_cast<std::int64_t>(header->ino) + entry->ino_off;
                    if (ino <= 0 || static_cast<std::uint64_t>(ino) > fs.superblock()->inodes)
                        return std::unexpected { lib::err::corrupted_data };

                    if (entry_cookie < cookie)
                        continue;

                    const auto ref = (static_cast<std::uint64_t>(header->start) << 16) | entry->offset;
                    auto cont = fn(name, ref, ino, entry->type, entry_cookie);
                    if (!cont)
                        return std::unexpected { cont.error() };
                    if (!*cont)
                        return { };
                }
            }
            return { };
        }

        auto instance_t::iget(
            const instance_ptr &handle, std::uint64_t reference,
            std::optional<std::uint32_t> expected_ino,
            std::optional<inode_type> expected_type
        ) -> lib::expect<std::shared_ptr<fs_inode_t>>
        {
            if (const auto it = icache.find(reference); it != icache.end())
            {
                if (auto inode = it->second.lock())
                {
                    if (expected_ino && inode->stat.st_ino != *expected_ino)
                        return std::unexpected { lib::err::corrupted_data };
                    if (expected_type && to_basic(inode->type) != *expected_type)
                        return std::unexpected { lib::err::corrupted_data };
                    return inode;
                }
                icache.erase(reference);
            }

            const auto rel_blk = reference >> 16;
            const std::size_t offset = reference & 0xFFFF;

            if (rel_blk > std::numeric_limits<std::uint32_t>::max() || offset >= metadata_size)
                return std::unexpected { lib::err::corrupted_data };

            const auto *sb = superblock();
            auto abs = table_location(sb->inode_table, sb->dir_table, rel_blk);
            if (!abs)
                return std::unexpected { abs.error() };

            const metadata_cursor_t start {
                .block = *abs,
                .offset = offset,
                .limit = sb->dir_table
            };

            auto cursor = start;
            auto base_res = read_metadata<inode_t>(cursor);
            if (!base_res)
                return std::unexpected { base_res.error() };

            const auto base = std::move(*base_res);
            if (!magic_enum::enum_contains(base.type) || (base.perms & stat::s_ifmt) != 0 ||
                base.uid >= ids.size() || base.gid >= ids.size() ||
                base.ino == 0 || base.ino > sb->inodes)
                return std::unexpected { lib::err::corrupted_data };

            if (expected_ino && base.ino != *expected_ino)
                return std::unexpected { lib::err::corrupted_data };
            if (expected_type && to_basic(base.type) != *expected_type)
                return std::unexpected { lib::err::corrupted_data };

            const mode_t mode = static_cast<mode_t>(base.perms) | [](inode_type type) {
                switch (to_basic(type))
                {
                    case inode_type::basic_dir:
                        return stat::s_ifdir;
                    case inode_type::basic_file:
                        return stat::s_ifreg;
                    case inode_type::basic_symlink:
                        return stat::s_iflnk;
                    case inode_type::basic_blkdev:
                        return stat::s_ifblk;
                    case inode_type::basic_chardev:
                        return stat::s_ifchr;
                    case inode_type::basic_fifo:
                        return stat::s_ififo;
                    case inode_type::basic_sock:
                        return stat::s_ifsock;
                    default:
                        std::unreachable();
                }
            } (base.type);

            std::uint32_t links = 1;
            std::uint64_t size = 0;
            std::uint64_t sparse = 0;
            dev_t rdev = 0;

            metadata_cursor_t symlink_cursor { };
            metadata_cursor_t dir_cursor { };
            metadata_cursor_t dir_index_cursor { };
            metadata_cursor_t block_list_cursor { };

            std::uint16_t dir_index_count = 0;
            std::uint64_t block_start = 0;
            std::uint32_t frag = no_frag;
            std::uint32_t frag_offset = 0;

            const auto decode_dev = [](std::uint32_t value) {
                return makedev((value >> 8) & 0xFFF, (value & 0xFF) | ((value >> 12) & ~0xFFu));
            };

            cursor = start;
            switch (base.type)
            {
                case inode_type::basic_dir:
                {
                    auto raw = read_metadata<dir_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    links = raw->links;
                    size = raw->size;
                    if (size > 3)
                    {
                        auto loc = table_location(sb->dir_table, dir_table_end, raw->block_idx);
                        if (!loc)
                            return std::unexpected { loc.error() };

                        dir_cursor = { *loc, raw->block_off, dir_table_end };
                    }
                    else dir_cursor = { sb->dir_table, raw->block_off, dir_table_end };
                    break;
                }
                case inode_type::extended_dir:
                {
                    auto raw = read_metadata<ext_dir_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    links = raw->links;
                    size = raw->size;
                    dir_index_count = raw->index_count;
                    dir_index_cursor = cursor;

                    if (size > 3)
                    {
                        auto loc = table_location(sb->dir_table, dir_table_end, raw->block_idx);
                        if (!loc)
                            return std::unexpected { loc.error() };

                        dir_cursor = { *loc, raw->block_off, dir_table_end };
                    }
                    else dir_cursor = { sb->dir_table, raw->block_off, dir_table_end };
                    break;
                }
                case inode_type::basic_file:
                {
                    auto raw = read_metadata<file_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    size = raw->size;
                    block_start = raw->block_start;
                    frag = raw->frag_idx;
                    frag_offset = raw->block_off;
                    block_list_cursor = cursor;
                    break;
                }
                case inode_type::extended_file:
                {
                    auto raw = read_metadata<ext_file_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    size = raw->size;
                    sparse = raw->sparse;
                    links = raw->links;
                    block_start = raw->blocks_start;
                    frag = raw->frag_idx;
                    frag_offset = raw->block_off;
                    block_list_cursor = cursor;
                    break;
                }
                case inode_type::basic_symlink:
                case inode_type::extended_symlink:
                {
                    auto raw = read_metadata<slink_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    size = raw->size;
                    links = raw->links;
                    symlink_cursor = cursor;

                    if (base.type == inode_type::extended_symlink)
                    {
                        auto xattr_cursor = cursor;
                        if (const auto ret = skip_metadata(xattr_cursor, size); !ret)
                            return std::unexpected { ret.error() };

                        if (const auto xattr = read_metadata<std::uint32_t>(xattr_cursor); !xattr)
                            return std::unexpected { xattr.error() };
                    }
                    break;
                }
                case inode_type::basic_blkdev:
                case inode_type::basic_chardev:
                {
                    auto raw = read_metadata<dev_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    links = raw->links;
                    rdev = decode_dev(raw->dev);
                    break;
                }
                case inode_type::extended_blkdev:
                case inode_type::extended_chardev:
                {
                    auto raw = read_metadata<ext_dev_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    links = raw->links;
                    rdev = decode_dev(raw->dev);
                    break;
                }
                case inode_type::basic_fifo:
                case inode_type::basic_sock:
                {
                    auto raw = read_metadata<ipc_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    links = raw->links;
                    break;
                }
                case inode_type::extended_fifo:
                case inode_type::extended_sock:
                {
                    auto raw = read_metadata<ext_ipc_inode_t>(cursor);
                    if (!raw)
                        return std::unexpected { raw.error() };

                    links = raw->links;
                    break;
                }
            }

            if (links == 0 || size > std::numeric_limits<off_t>::max() || sparse > size)
                return std::unexpected { lib::err::corrupted_data };

            if (stat::type(mode) == stat::s_ifdir &&
                (size < 3 || dir_cursor.offset >= metadata_size ||
                (size == 3 && dir_index_count != 0)))
                return std::unexpected { lib::err::corrupted_data };

            if (stat::type(mode) == stat::s_iflnk && (size == 0 || size > max_symlen))
                return std::unexpected { lib::err::corrupted_data };

            if (stat::type(mode) == stat::s_ifreg && frag != no_frag &&
                (frag >= sb->frags || size % sb->block_size == 0))
                return std::unexpected { lib::err::corrupted_data };

            std::shared_ptr<vfs::ops_t> ops;
            switch (stat::type(mode))
            {
                case stat::s_ifdir:
                case stat::s_ifreg:
                case stat::s_iflnk:
                    ops = ops_t::singleton();
                    break;
                default:
                    break;
            }

            auto inode = std::make_shared<fs_inode_t>(handle, base.type, std::move(ops));
            {
                inode->stat.st_dev = dev_id;
                inode->stat.st_ino = base.ino;
                inode->stat.st_nlink = links;
                inode->stat.st_mode = mode;
                inode->stat.st_uid = ids[base.uid];
                inode->stat.st_gid = ids[base.gid];
                inode->stat.st_rdev = rdev;
                inode->stat.st_size = size;
                inode->stat.st_blksize = sb->block_size;
                inode->stat.st_blocks = stat::type(mode) == stat::s_ifreg
                    ? lib::div_roundup(size - sparse, 512) : 0;

                const timespec timestamp { base.mtime, 0 };
                inode->stat.st_atim = timestamp;
                inode->stat.st_mtim = timestamp;
                inode->stat.st_ctim = timestamp;

                inode->symlink_cursor = symlink_cursor;
                inode->dir_cursor = dir_cursor;
                inode->dir_index_cursor = dir_index_cursor;
                inode->block_list_cursor = block_list_cursor;

                inode->dir_index_count = dir_index_count;
                inode->block_start = block_start;
                inode->frag = frag;
                inode->frag_offset = frag_offset;
                inode->sparse = sparse;
            }

            if (icache.size() >= icache_clean && (icache.size() % icache_clean) == 0)
            {
                std::vector<std::uint64_t> expired;
                expired.reserve(icache.size());
                for (const auto &[ref, weak] : icache)
                {
                    if (weak.expired())
                        expired.push_back(ref);
                }
                for (const auto ref : expired)
                    icache.erase(ref);
            }

            icache[reference] = inode;
            return inode;
        }

        lib::expect<std::size_t> ops_t::read(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        )
        {
            // TODO
            lib::unused(file, offset, buffer);
            return std::unexpected { lib::err::not_implemented };
        }

        // TODO
        // lib::expect<vmm::object::ptr> ops_t::map(std::shared_ptr<vfs::file_t> file)
        // {
        //     return std::unexpected { lib::err::not_implemented };
        // }

        auto instance_t::readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
            -> lib::expect<lib::list<vfs::dir_entry>>
        {
            lib::list<vfs::dir_entry> entries;
            const auto inode = get_inode(dir);

            const auto ret = walk_dir(*this, inode, cookie, std::nullopt,
                [&](std::string_view name, std::uint64_t reference, std::uint32_t ino,
                    inode_type type, std::size_t entry_cookie) -> lib::expect<bool>
                {
                    auto child = iget(inode->handle, reference, ino, type);
                    if (!child)
                        return std::unexpected { child.error() };

                    entries.emplace_back(std::string { name }, std::move(*child), entry_cookie);
                    return entries.size() < max_readdir_batch;
                }
            );
            if (!ret)
                return std::unexpected { ret.error() };
            return entries;
        }

        auto instance_t::lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
            -> lib::expect<vfs::dir_entry>
        {
            if (name.empty() || name.size() > max_namelen || name == "." || name == ".." ||
                name.contains('\0') || name.contains('/'))
                return std::unexpected { lib::err::not_found };

            std::optional<vfs::dir_entry> result;
            const auto inode = get_inode(dir);

            const auto ret = walk_dir(*this, inode, 3, name,
                [&](std::string_view entry_name, std::uint64_t reference, std::uint32_t ino,
                    inode_type type, std::size_t entry_cookie) -> lib::expect<bool>
                {
                    if (entry_name < name)
                        return true;
                    if (entry_name > name)
                        return false;

                    auto child = iget(inode->handle, reference, ino, type);
                    if (!child)
                        return std::unexpected { child.error() };

                    result.emplace(std::string { entry_name }, std::move(*child), entry_cookie);
                    return false;
                }
            );
            if (!ret)
                return std::unexpected { ret.error() };
            if (!result)
                return std::unexpected { lib::err::not_found };
            return std::move(*result);
        }

        auto instance_t::readlink(std::shared_ptr<vfs::dentry_t> dentry) -> lib::expect<lib::path>
        {
            const auto inode = get_inode(dentry);
            if (inode->stat.type() != stat::s_iflnk)
                return std::unexpected { lib::err::invalid_argument };

            std::string target(inode->stat.st_size, '\0');

            auto cursor = inode->symlink_cursor;
            auto bytes = std::as_writable_bytes(std::span { target.data(), target.size() });

            if (const auto ret = read_metadata(cursor, bytes); !ret)
                return std::unexpected { ret.error() };
            if (target.contains('\0'))
                return std::unexpected { lib::err::corrupted_data };

            return std::move(target);
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
            lib::unused(data, flags);

            auto file = vfs::file_t::create({ nullptr, src }, 0, 0);
            if (const auto ret = file->open(0, sched::current_process()->pid); !ret.has_value())
                return std::unexpected { ret.error() };

            auto sbres = file->read_obj<superblock_t>(0);
            if (!sbres.has_value())
                return std::unexpected { sbres.error() };

            auto sbuf = std::move(*sbres);
            const auto *sb = sbuf.data();

            if (sb->magic != magic)
                return std::unexpected { lib::err::invalid_argument };

            if (sb->vermaj != version_major || sb->vermin != version_minor)
                return std::unexpected { lib::err::invalid_argument };

            if (sb->inodes == 0 || sb->ids == 0 || sb->bytes_used < sizeof(superblock_t))
                return std::unexpected { lib::err::invalid_argument };

            if (sb->block_log < std::countr_zero(min_datablk) ||
                sb->block_log > std::countr_zero(max_datablk) ||
                !std::has_single_bit(sb->block_size) ||
                sb->block_size < min_datablk || sb->block_size > max_datablk ||
                sb->block_size != (1u << sb->block_log))
                return std::unexpected { lib::err::invalid_argument };

            const auto raw_flags = std::to_underlying(sb->flags);
            if ((raw_flags != 0 && !magic_enum::enum_flags_contains<flag>(raw_flags)) ||
                magic_enum::enum_flags_test(sb->flags, flag::check_data))
                return std::unexpected { lib::err::invalid_argument };

            if (sb->inode_table < sizeof(superblock_t) || sb->inode_table >= sb->dir_table ||
                sb->dir_table >= sb->id_table || sb->id_table >= sb->bytes_used)
                return std::unexpected { lib::err::invalid_argument };

            const auto id_index_size = lib::div_roundup(
                static_cast<std::size_t>(sb->ids) * sizeof(std::uint32_t),
                metadata_size
            ) * sizeof(std::uint64_t);

            if (id_index_size > sb->bytes_used - sb->id_table)
                return std::unexpected { lib::err::invalid_argument };

            const auto root_block = sb->root_ino >> 16;
            const auto root_offset = sb->root_ino & 0xFFFF;
            const auto inode_table_size = sb->dir_table - sb->inode_table;
            if (inode_table_size < sizeof(std::uint16_t) ||
                root_block > inode_table_size - sizeof(std::uint16_t) ||
                root_offset >= metadata_size)
                return std::unexpected { lib::err::invalid_argument };

            if ((raw_flags & std::to_underlying(flag::no_frags)) && sb->frags != 0)
                return std::unexpected { lib::err::invalid_argument };

            const auto valid_lookup_table = [&](std::uint64_t table, std::uint64_t data_size) {
                const auto indexes = lib::div_roundup(data_size, metadata_size);
                const auto index_size = indexes * sizeof(std::uint64_t);
                return table != no_table && table < sb->bytes_used &&
                    index_size <= sb->bytes_used - table;
            };

            if (sb->frags != 0 && !valid_lookup_table(
                sb->frag_table, static_cast<std::uint64_t>(sb->frags) * sizeof(fragment_t)))
                return std::unexpected { lib::err::invalid_argument };

            if ((sb->flags & flag::has_export_table) != flag::none && !valid_lookup_table(
                sb->export_table, static_cast<std::uint64_t>(sb->inodes) * sizeof(std::uint64_t)))
                return std::unexpected { lib::err::invalid_argument };

            lib::compression_format fmt;
            switch (sb->compressor)
            {
                case compressor::zlib:
                    fmt = lib::compression_format::zlib;
                    break;
                case compressor::lz4:
                    fmt = lib::compression_format::lz4;
                    break;
                default:
                    return std::unexpected { lib::err::not_supported };
            }

            auto decompressor = lib::decompressor::create(fmt);
            if (!decompressor)
                return std::unexpected { decompressor.error() };

            std::shared_ptr<vfs::dentry_t> root;
            auto instance = lib::make_locked<squashfs::instance_t, sched::mutex_t>(
                std::move(file), std::move(sbuf), std::move(*decompressor)
            );
            {
                auto locked = instance.lock();
                locked->fs = const_cast<fs_t *>(this);

                if (const auto ret = locked->initialise(); !ret)
                    return std::unexpected { ret.error() };

                auto rres = locked->iget(instance, locked->superblock()->root_ino);
                if (!rres.has_value())
                    return std::unexpected { rres.error() };

                if ((*rres)->stat.type() != stat::s_ifdir)
                    return std::unexpected { lib::err::corrupted_data };

                root = std::make_shared<vfs::dentry_t>();
                root->name = "squashfs root";
                root->inode = std::move(*rres);
                root->parent = root;
            }

            auto mount = std::make_shared<struct vfs::mount_t>(std::move(instance), root);
            mount->flags = vfs::ms_rdonly;
            return mount;
        }

        fs_t() : vfs::filesystem_t { "squashfs", squashfs::magic, true } { }
    } filesystem;
} // namespace squashfs

filesystem_module(
    "squashfs", "Compressed Read-Only Filesystem",
    squashfs::filesystem
);
