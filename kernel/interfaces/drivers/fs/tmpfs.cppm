// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.tmpfs;

import system.memory.virt;
import system.vfs;
import lib;
import std;

export namespace fs::tmpfs
{
    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override;

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override;

        lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file> file) override;
    };

    struct fs : vfs::filesystem
    {
        struct instance : vfs::filesystem::instance, std::enable_shared_from_this<instance>
        {
            std::size_t max_size = std::numeric_limits<std::size_t>::max();
            std::size_t max_inodes = std::numeric_limits<std::size_t>::max();
            std::atomic<std::size_t> current_size = 0;
            std::atomic<std::size_t> current_inodes = 0;

            mode_t opt_mode = 0777 | s_isvtx;
            uid_t opt_uid = 0;
            gid_t opt_gid = 0;

            auto create(
                std::shared_ptr<vfs::inode> &parent,
                std::string_view name, mode_t mode, dev_t rdev
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override;

            auto symlink(
                std::shared_ptr<vfs::inode> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override;

            auto link(
                std::shared_ptr<vfs::inode> &parent,
                std::string_view name, std::shared_ptr<vfs::inode> target
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override;

            auto unlink(std::shared_ptr<vfs::inode> &node) -> lib::expect<void> override;

            auto rename(
                std::shared_ptr<vfs::inode> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode> replaced
            ) -> lib::expect<void> override;

            auto readdir(std::shared_ptr<vfs::dentry> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override;

            auto lookup(std::shared_ptr<vfs::dentry> dir,std::string_view name)
                -> lib::expect<std::optional<vfs::dir_entry>> override;

            auto write_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void> override;
            auto dirty_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void> override;

            bool sync() override;

            bool unmount(std::shared_ptr<struct vfs::mount>) override;

            std::string mount_options() const override;

            ~instance() = default;
        };

        mutable lib::list<std::shared_ptr<struct vfs::mount>> mounts;
        auto mount(
            std::shared_ptr<vfs::dentry> src,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount>> override;

        fs() : vfs::filesystem { "tmpfs" } { }
    };

    struct inode : vfs::inode
    {
        fs::instance *owner;
        vmm::object::ptr memory;
        inode(fs::instance *owner, dev_t dev, dev_t rdev, ino_t ino, mode_t mode);
        ~inode();
    };

    lib::initgraph::stage *registered_stage();
} // export namespace fs::tmpfs
