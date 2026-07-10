// Copyright (C) 2024-2026  ilobilo

import system.vfs;
import lib;
import std;

namespace ext2
{
    struct fs_t : vfs::filesystem_t
    {
        struct instance_t : vfs::filesystem_t::instance_t
        {
            auto create(
                std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                mode_t mode, dev_t rdev, std::optional<std::shared_ptr<vfs::ops_t>> ops
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                // TODO
                lib::unused(parent, name, mode, rdev, ops);
                return std::unexpected { lib::err::todo };
            }

            auto symlink(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                // TODO
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::todo };
            }

            auto link(
                std::shared_ptr<vfs::inode_t> &parent,
                std::string_view name, std::shared_ptr<vfs::inode_t> target
            ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
            {
                // TODO
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::todo };
            }

            auto unlink(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode);
                return std::unexpected { lib::err::todo };
            }

            auto rename(
                std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode_t> replaced
            ) -> lib::expect<void> override
            {
                // TODO
                lib::unused(old_parent, old_name, new_parent, new_name, replaced);
                return std::unexpected { lib::err::todo };
            }

            auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override
            {
                // TODO
                lib::unused(dir, cookie);
                return std::unexpected { lib::err::todo };
            }

            auto lookup(std::shared_ptr<vfs::dentry_t> dir,std::string_view name)
                -> lib::expect<vfs::dir_entry> override
            {
                // TODO
                lib::unused(dir, name);
                return std::unexpected { lib::err::todo };
            }

            auto readlink(std::shared_ptr<vfs::dentry_t> dentry) -> lib::expect<lib::path> override
            {
                // TODO
                lib::unused(dentry);
                return std::unexpected { lib::err::todo };
            }

                // TODO
            std::string mount_options() const override { return { }; }

            void statfs(struct ::statfs &out) override
            {
                // TODO
                lib::unused(out);
            }

            auto write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode);
                return std::unexpected { lib::err::todo };
            }

            auto dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode);
                return std::unexpected { lib::err::todo };
            }

            auto getxattr(std::shared_ptr<vfs::inode_t> &inode, std::string_view name)
                -> lib::expect<lib::membuffer> override
            {
                // TODO
                lib::unused(inode, name);
                return std::unexpected { lib::err::todo };
            }

            auto setxattr(
                std::shared_ptr<vfs::inode_t> &inode, std::string_view name,
                lib::maybe_uspan<std::byte> data, int flags
            ) -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode, name, data, flags);
                return std::unexpected { lib::err::todo };
            }

            auto remxattr(std::shared_ptr<vfs::inode_t> &inode, std::string_view name)
                -> lib::expect<void> override
            {
                // TODO
                lib::unused(inode, name);
                return std::unexpected { lib::err::todo };
            }

            auto listxattrs(std::shared_ptr<vfs::inode_t> &inode)
                -> lib::expect<std::vector<std::string>> override
            {
                // TODO
                lib::unused(inode);
                return std::unexpected { lib::err::todo };
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
                return false;
            }

            ~instance_t() = default;
        };

        mutable lib::list<std::shared_ptr<struct vfs::mount_t>> mounts;
        auto mount(
            std::shared_ptr<vfs::dentry_t> src,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
        {
            // TODO
            lib::unused(src, data);
            return std::unexpected { lib::err::todo };
        }

        fs_t() : vfs::filesystem_t { "ext2", 0xEF53 } { }
    } filesystem;
} // namespace ext2

filesystem_module(
    "ext2", "Second Extended Filesystem",
    ext2::filesystem
);
