// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.tmpfs;

import system.memory.virt;
import system.vfs;
import lib;
import std;

export namespace fs::tmpfs
{
    struct ops : vfs::ops_t
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        bool truncable() const override { return true; }
        lib::expect<void> trunc(std::shared_ptr<vfs::file_t> file, std::size_t size) override;

        lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file_t> file) override;
    };

    struct fs_t : vfs::filesystem_t
    {
        struct instance : vfs::filesystem_t::instance_t, std::enable_shared_from_this<instance>
        {
            std::size_t max_size = std::numeric_limits<std::size_t>::max();
            std::size_t max_inodes = std::numeric_limits<std::size_t>::max();
            std::atomic<std::size_t> current_size = 0;
            std::atomic<std::size_t> current_inodes = 0;

            mode_t opt_mode = 0777 | s_isvtx;
            uid_t opt_uid = 0;
            gid_t opt_gid = 0;

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

            auto unlink(std::shared_ptr<vfs::inode_t> &node) -> lib::expect<void> override;

            auto rename(
                std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode_t> replaced
            ) -> lib::expect<void> override;

            auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override;

            auto lookup(std::shared_ptr<vfs::dentry_t> dir,std::string_view name)
                -> lib::expect<vfs::dir_entry> override;

            auto write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override;
            auto dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override;

            bool sync() override;

            bool unmount(std::shared_ptr<struct vfs::mount_t>) override;

            std::string mount_options() const override;
            void statfs(struct ::statfs &out) override;

            ~instance() = default;
        };

        mutable lib::list<std::shared_ptr<struct vfs::mount_t>> mounts;
        auto mount(
            std::shared_ptr<vfs::dentry_t> src,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override;

        fs_t() : vfs::filesystem_t { "tmpfs", 0x01021994 } { }
    };

    struct inode_t : vfs::inode_t
    {
        fs_t::instance *owner;
        vmm::object::ptr memory;
        inode_t(
            fs_t::instance *owner, dev_t dev, dev_t rdev,
            ino_t ino, mode_t mode, std::shared_ptr<vfs::ops_t> ops
        );
        ~inode_t();
    };

    lib::initgraph::stage *registered_stage();
} // export namespace fs::tmpfs
