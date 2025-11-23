// Copyright (C) 2024-2025  ilobilo

export module drivers.fs.tmpfs;

import system.memory.virt;
import system.vfs;
import cppstd;
import lib;

export namespace fs::tmpfs
{
    struct inode : vfs::inode
    {
        std::shared_ptr<vmm::object> memory;
        inode(dev_t dev, ino_t ino, mode_t mode, std::shared_ptr<vfs::ops> op);
    };

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override;
        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override;

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override;

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::file> file, bool priv) override;

        bool sync() override;
    };

    struct fs : vfs::filesystem
    {
        struct instance : vfs::filesystem::instance, std::enable_shared_from_this<instance>
        {
            auto create(std::shared_ptr<vfs::inode> &parent, std::string_view name, mode_t mode, std::shared_ptr<vfs::ops> ops) -> vfs::expect<std::shared_ptr<vfs::inode>> override;
            auto symlink(std::shared_ptr<vfs::inode> &parent, std::string_view name, lib::path target) -> vfs::expect<std::shared_ptr<vfs::inode>> override;
            auto link(std::shared_ptr<vfs::inode> &parent, std::string_view name, std::shared_ptr<vfs::inode> target) -> vfs::expect<std::shared_ptr<vfs::inode>> override;
            auto unlink(std::shared_ptr<vfs::inode> &node) -> vfs::expect<void> override;

            auto populate(std::shared_ptr<vfs::inode> &node, std::string_view name = "") -> vfs::expect<std::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>> override;
            bool sync() override;

            bool unmount(std::shared_ptr<struct vfs::mount>) override;

            ~instance() = default;
        };

        mutable std::list<std::shared_ptr<struct vfs::mount>> mounts;
        auto mount(std::shared_ptr<vfs::dentry> src) const -> vfs::expect<std::shared_ptr<struct vfs::mount>> override;

        fs() : vfs::filesystem { "tmpfs" } { }
    };

    lib::initgraph::stage *registered_stage();
} // export namespace fs::tmpfs