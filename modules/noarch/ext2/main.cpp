// Copyright (C) 2024-2026  ilobilo

import system.memory.virt;
import system.sched;
import system.vfs.dev;
import system.vfs;
import lib;
import std;

import ext2;

namespace ext2
{
    namespace
    {
        template<typename Type>
        auto read_at(auto &file, std::uint64_t offset, std::size_t count = 1)
            -> lib::expect<lib::buffer<Type>>
        {
            lib::buffer<Type> buffer { count };
            auto uspan = buffer.byte_uspan();
            lib::bug_on(!uspan);

            const auto ret = file->pread(offset, *uspan);
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            if (*ret != buffer.size_bytes())
                return std::unexpected { lib::err::invalid_argument };
            return buffer;
        }

        struct fs_inode_t;
        struct instance_t : vfs::filesystem_t::instance_t
        {
            std::shared_ptr<vfs::file_t> src;
            lib::buffer<superblock_t> sb;
            lib::buffer<group_desc_t> gds;

            std::uint32_t block_size;
            std::uint16_t inode_size;

            lib::map::flat_hash<ino_t, std::weak_ptr<fs_inode_t>> icache;
            std::uint64_t flags;

            instance_t(
                std::shared_ptr<vfs::file_t> src,
                lib::buffer<superblock_t> sb, lib::buffer<group_desc_t> gds,
                std::uint32_t block_size, std::uint16_t inode_size, std::uint64_t flags
            ) : src { std::move(src) }, sb { std::move(sb) }, gds { std::move(gds) },
                block_size { block_size }, inode_size { inode_size },
                icache { }, flags { flags } { }

            superblock_t *superblock() { return sb.data(); }

            template<typename Type>
            auto read_at(std::uint64_t offset, std::size_t count = 1)
                -> lib::expect<lib::buffer<Type>>
            {
                return ext2::read_at<Type>(src, offset, count);
            }

            auto read_ino(ino_t ino) -> lib::expect<lib::buffer<inode_t>>
            {
                const auto ipb = superblock()->inodes_per_group;
                const auto group = (ino - 1) / ipb;
                const auto index = (ino - 1) % ipb;
                const auto offset = gds.at(group).inode_table * block_size + index * inode_size;
                return read_at<inode_t>(offset);
            }

            auto iget(ino_t ino) -> lib::expect<std::shared_ptr<fs_inode_t>>;

            auto bmap(ext2::inode_t *inode, std::uint32_t num)
                -> lib::expect<std::pair<std::uint32_t, std::uint32_t>>
            {
                const std::uint64_t ppb = block_size / sizeof(std::uint32_t);

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

            auto walk_dir(ext2::inode_t *inode, std::uint64_t start, auto &&fn) -> lib::expect<void>
            {
                const std::uint64_t size = inode->size;
                for (std::uint64_t off = start; off < size; )
                {
                    const auto lblk = off / block_size;

                    auto res = bmap(inode, lblk);
                    if (!res.has_value())
                        return std::unexpected { res.error() };
                    const std::uint64_t phys = res->first;

                    const auto block_start = lblk * block_size;
                    if (phys == 0)
                    {
                        off = block_start + block_size;
                        continue;
                    }

                    auto blk = read_at<std::byte>(phys * block_size, block_size);
                    if (!blk.has_value())
                        return std::unexpected { blk.error() };
                    const auto *data = blk->data();

                    for (std::uint32_t i = 0; i < block_size; )
                    {
                        const auto *de = reinterpret_cast<const dir_entry_2_t *>(data + i);
                        const auto rec = de->rec_len;
                        if (rec < sizeof(dir_entry_2_t) || i + rec > block_size)
                            return std::unexpected { lib::err::corrupted_data };

                        const auto abs = block_start + i;
                        if (abs >= start && de->inode != 0)
                        {
                            const std::string_view name { de->name, de->name_len };
                            if (name != "." && name != "..")
                            {
                                auto cont = fn(name, de->inode, abs);
                                if (!cont.has_value())
                                    return std::unexpected { cont.error() };
                                if (!*cont)
                                    return { };
                            }
                        }
                        i += rec;
                    }
                    off = block_start + block_size;
                }
                return { };
            }

            auto read_data(
                ext2::inode_t *inode, std::uint64_t offset,
                lib::maybe_uspan<std::byte> dst
            ) -> lib::expect<std::size_t>
            {
                const auto total = dst.size_bytes();
                std::size_t progress = 0;

                while (progress < total)
                {
                    const auto pos  = offset + progress;
                    const auto lblk = pos / block_size;
                    const auto boff = pos % block_size;

                    auto res = bmap(inode, lblk);
                    if (!res.has_value())
                        return std::unexpected { res.error() };
                    const auto [phys, run] = *res;

                    const auto run_bytes = static_cast<std::uint64_t>(run) * block_size - boff;
                    const auto chunk = std::min(run_bytes, total - progress);

                    auto out = dst.subspan(progress, chunk);
                    if (phys == 0)
                    {
                        if (!out.fill(0, chunk))
                            return std::unexpected { lib::err::invalid_address };
                    }
                    else
                    {
                        auto blk = read_at<std::byte>(
                            static_cast<std::uint64_t>(phys) * block_size + boff, chunk
                        );
                        if (!blk.has_value())
                            return std::unexpected { blk.error() };
                        if (!out.copy_from(blk->span()))
                            return std::unexpected { lib::err::invalid_address };
                    }
                    progress += chunk;
                }
                return progress;
            }

            auto create(
                std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                mode_t mode, dev_t rdev, std::optional<std::shared_ptr<vfs::ops_t>> ops
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                // TODO
                lib::unused(parent, name, mode, rdev, ops);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto symlink(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                // TODO
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto link(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, std::shared_ptr<vfs::inode_t> target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                // TODO
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto unlink(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto rename(
                std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode_t> replaced
            ) -> lib::expect<void> override
            {
                // TODO
                lib::unused(old_parent, old_name, new_parent, new_name, replaced);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override;


            auto lookup(std::shared_ptr<vfs::dentry_t> dir,std::string_view name)
                -> lib::expect<vfs::dir_entry> override;

            auto readlink(std::shared_ptr<vfs::dentry_t> dentry) -> lib::expect<lib::path> override;

            std::string mount_options() const override
            {
                // TODO
                return "ro";
            }

            void statfs(struct ::statfs &out) override
            {
                const auto *sb = superblock();

                out.f_type = magic;
                out.f_bsize = block_size;
                out.f_blocks = sb->blocks_count;
                out.f_bfree = sb->free_blocks_count;
                out.f_bavail = out.f_bfree;
                out.f_files = sb->inodes_count;
                out.f_ffree = sb->free_inodes_count;
                // out.f_fsid = ;
                out.f_namelen = name_len;
                out.f_frsize = block_size;
                out.f_flags = flags;
            }

            auto write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode);
                return { };
            }

            auto dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                // TODO
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

            bool sync() override
            {
                // TODO
                return true;
            }

            bool unmount(std::shared_ptr<vfs::mount_t> mnt) override
            {
                // TODO
                lib::unused(mnt);
                return true;
            }

            ~instance_t() = default;
        };

        struct ops_t : vfs::ops_t
        {
            // TODO
            bool truncable() const override { return false; }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override;

            lib::expect<std::size_t> write(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override;

            // lib::expect<void> trunc(std::shared_ptr<vfs::file_t> file, std::size_t size) override
            // {
            //     // TODO
            //     lib::unused(file, size);
            //     return std::unexpected { lib::err::todo };
            // }

            // lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file_t> file) override
            // {
            //     // TODO
            //     lib::unused(file);
            //     return std::unexpected { lib::err::todo };
            // }

            // lib::expect<void> sync() override
            // {
            //     // TODO
            //     return std::unexpected { lib::err::todo };
            // }

            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }
        };

        struct fs_inode_t : vfs::inode_t
        {
            instance_t *owner;
            lib::buffer<ext2::inode_t> ino;

            ext2::inode_t *inode() { return ino.data(); }

            fs_inode_t(instance_t *owner, decltype(ino) _ino, ino_t id)
                : vfs::inode_t { ops_t::singleton() }, owner { owner }, ino { std::move(_ino) }
            {
                const auto *ino = inode();

                stat.st_dev = owner->dev_id;
                stat.st_ino = id;
                stat.st_nlink = ino->links_count;
                stat.st_mode = ino->mode;
                stat.st_uid = ino->uid;
                stat.st_gid = ino->gid;

                if (stat.type() == stat::s_ifchr || stat.type() == stat::s_ifblk)
                    stat.st_rdev = ino->block[0] ? ino->block[0] : ino->block[1];
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
        };

        lib::expect<std::size_t> ops_t::read(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        )
        {
            // TODO
            auto inode = std::static_pointer_cast<fs_inode_t>(file->path.dentry->inode);

            const auto file_size = static_cast<std::uint64_t>(inode->stat.st_size);
            if (offset >= file_size)
                return 0;

            const auto real_size = std::min(buffer.size_bytes(), file_size - offset);
            if (real_size == 0)
                return 0;

            return inode->owner->read_data(inode->inode(), offset, buffer.subspan(0, real_size));
        }

        lib::expect<std::size_t> ops_t::write(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        )
        {
            // TODO
            lib::unused(file, offset, buffer);
            return std::unexpected { lib::err::read_only_fs };
        }

        auto instance_t::iget(ino_t ino) -> lib::expect<std::shared_ptr<fs_inode_t>>
        {
            if (auto it = icache.find(ino); it != icache.end())
            {
                if (auto sp = it->second.lock())
                    return sp;
            }

            auto res = read_ino(ino);
            if (!res.has_value())
                return std::unexpected { res.error() };

            auto node = std::make_shared<fs_inode_t>(this, std::move(*res), ino);
            icache.insert_or_assign(ino, node);
            return node;
        }

        auto instance_t::readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
            -> lib::expect<lib::list<vfs::dir_entry>>
        {
            constexpr std::size_t max_batch = 256;
            lib::list<vfs::dir_entry> out;

            auto ret = walk_dir(
                std::static_pointer_cast<fs_inode_t>(dir->inode)->inode(), cookie,
                [&](std::string_view name, ino_t ino, std::uint64_t off) -> lib::expect<bool>
                {
                    auto child = iget(ino);
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
            auto ret = walk_dir(
                std::static_pointer_cast<fs_inode_t>(dir->inode)->inode(), 0,
                [&](std::string_view ename, ino_t ino, std::uint64_t off) -> lib::expect<bool>
                {
                    lib::unused(off);

                    if (ename != name)
                        return true;

                    auto child = iget(ino);
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
            const auto inode = std::static_pointer_cast<fs_inode_t>(dentry->inode)->inode();
            if (inode->size < 60 && inode->blocks == 0)
                return std::string_view { reinterpret_cast<char *>(inode->block), inode->size };

            lib::membuffer buf { inode->size };
            auto us = buf.byte_uspan();
            lib::bug_on(!us);

            auto ret = read_data(inode, 0, *us);
            if (!ret.has_value())
                return std::unexpected { ret.error() };
            return std::string_view { reinterpret_cast<char *>(buf.data()), inode->size };
        }
    } // namespace

    struct fs_t : vfs::filesystem_t
    {
        mutable lib::list<std::shared_ptr<struct vfs::mount_t>> mounts;
        auto mount(
            std::shared_ptr<vfs::dentry_t> src, std::uint64_t flags,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
        {
            // TODO
            lib::unused(data);

            auto file = vfs::file_t::create({ nullptr, src }, 0, 0);
            if (const auto ret = file->open(0, sched::current_process()->pid); !ret)
                return std::unexpected { ret.error() };

            auto sbres = read_at<superblock_t>(file, superblock_start);
            if (!sbres.has_value())
                return std::unexpected { sbres.error() };
            auto sbuf = std::move(*sbres);
            const auto *sb = sbuf.data();

            if (sb->magic != magic)
                return std::unexpected { lib::err::invalid_argument };

            // TODO: filesystems without filetype?
            constexpr auto incompat_feats = feature_incompat_filetype;
            if ((sb->feature_incompat & incompat_feats) != incompat_feats)
                return std::unexpected { lib::err::invalid_argument };
            if (sb->feature_incompat & ~incompat_feats)
                return std::unexpected { lib::err::invalid_argument };

            constexpr auto ro_feats = feature_ro_compat_sparse_super | feature_ro_compat_large_file;
            if (!(flags & vfs::ms_rdonly) && (sb->feature_ro_compat & ~ro_feats))
                return std::unexpected { lib::err::invalid_argument };

            const auto block_size = 1024 << sb->log_block_size;
            const auto inode_size = sb->rev_level >= 1 ? sb->inode_size : 128;

            const auto group_count = lib::div_roundup(
                sb->blocks_count - sb->first_data_block,
                sb->blocks_per_group
            );

            auto gdres = read_at<group_desc_t>(
                file, (sb->first_data_block + 1) * block_size, group_count
            );
            if (!gdres.has_value())
                return std::unexpected { gdres.error() };

            std::shared_ptr<vfs::dentry_t> root;

            auto instance = lib::make_locked<ext2::instance_t, sched::mutex>(
                std::move(file), std::move(sbuf), std::move(*gdres),
                block_size, inode_size, flags
            );
            {
                auto locked = instance.lock();

                auto rres = locked->iget(root_ino);
                if (!rres.has_value())
                    return std::unexpected { rres.error() };

                root = std::make_shared<vfs::dentry_t>();
                root->name = "ext2 root";
                root->inode = std::move(*rres);
                root->parent = root;
            }

            auto mount = std::make_shared<struct vfs::mount_t>(std::move(instance), root);
            mounts.push_back(mount);
            return mount;
        }

        fs_t() : vfs::filesystem_t { "ext2", ext2::magic, true } { }
    } filesystem;
} // namespace ext2

filesystem_module(
    "ext2", "Second Extended Filesystem",
    ext2::filesystem
);
