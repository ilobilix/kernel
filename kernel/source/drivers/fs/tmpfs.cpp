// Copyright (C) 2024-2025  ilobilo

module drivers.fs.tmpfs;

import system.memory;
import system.vfs;
import lib;
import cppstd;

namespace fs::tmpfs
{
    struct inode : vfs::inode
    {
        inode(ino_t ino, mode_t mode, std::shared_ptr<vfs::ops> op) : vfs::inode { op }
        {
            memory = std::make_shared<vmm::memobject>();

            stat.st_size = 0;
            stat.st_blocks = 0;
            stat.st_blksize = 512;

            // TODO
            // stat.st_dev = ;

            stat.st_ino = ino;
            stat.st_mode = mode;
            stat.st_nlink = 1;

            // TODO
            // stat.st_uid = 0;
            // stat.st_gid = 0;

            // TODO: time
            // stat.st_atim = ;
            // stat.st_mtim = ;
            // stat.st_ctim = ;
        }
    };

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::inode> self, std::uint64_t offset, std::span<std::byte> buffer) override
        {
            auto back = reinterpret_cast<inode *>(self.get());
            const std::unique_lock _ { back->lock };

            auto size = buffer.size_bytes();
            auto real_size = size;
            if (offset + size >= static_cast<std::size_t>(back->stat.st_size))
                real_size = size - ((offset + size) - back->stat.st_size);

            if (real_size == 0)
                return 0;

            self->memory->read(offset, buffer.subspan(0, real_size));
            return real_size;
        }

        std::ssize_t write(std::shared_ptr<vfs::inode> self, std::uint64_t offset, std::span<std::byte> buffer) override
        {
            auto back = reinterpret_cast<inode *>(self.get());
            const std::unique_lock _ { back->lock };

            auto size = buffer.size_bytes();
            self->memory->write(offset, buffer.subspan(0, size));

            if (offset + size >= static_cast<std::size_t>(back->stat.st_size))
            {
                back->stat.st_size = offset + size;
                back->stat.st_blocks = lib::div_roundup(offset + size, static_cast<std::size_t>(back->stat.st_blksize));
            }
            return size;
        }

        bool trunc(std::shared_ptr<vfs::inode> self, std::size_t size) override
        {
            auto back = reinterpret_cast<inode *>(self.get());
            const std::unique_lock _ { back->lock };

            const auto current_size = static_cast<std::size_t>(back->stat.st_size);
            if (size == current_size)
                return true;

            if (size < current_size)
                self->memory->clear(size, 0, current_size - size);
            else
                self->memory->clear(current_size, 0, size - current_size);

            back->stat.st_size = size;
            back->stat.st_blocks = lib::div_roundup(size, static_cast<std::size_t>(back->stat.st_blksize));
            return true;
        }

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::inode> self, bool priv) override
        {
            if (priv)
            {
                auto memory = std::make_shared<vmm::memobject>();
                self->memory->copy_to(*memory, 0, static_cast<std::size_t>(self->stat.st_size));
                return memory;
            }
            return self->memory;
        }

        bool sync() override { return true; }
    };

    struct fs : vfs::filesystem
    {
        struct instance : vfs::filesystem::instance, std::enable_shared_from_this<instance>
        {
            std::atomic<ino_t> inode_num = 0;

            auto create(std::shared_ptr<vfs::inode> &parent, std::string_view name, mode_t mode, std::shared_ptr<vfs::ops> ops) -> vfs::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name);
                return std::shared_ptr<vfs::inode>(new inode { inode_num++, mode, ops ?: ops::singleton() });
            }

            auto symlink(std::shared_ptr<vfs::inode> &parent, std::string_view name, lib::path target) -> vfs::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(target);
                return create(parent, name, static_cast<mode_t>(stat::type::s_iflnk), nullptr);
            }

            auto link(std::shared_ptr<vfs::inode> &parent, std::string_view name, std::shared_ptr<vfs::inode> target) -> vfs::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name);
                target->stat.st_nlink++;
                return target;
            }

            auto unlink(std::shared_ptr<vfs::inode> &node) -> vfs::expect<void> override
            {
                node->stat.st_nlink--;
                return { };
            }

            auto populate(std::shared_ptr<vfs::inode> &node, std::string_view name = "") -> vfs::expect<std::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>> override
            {
                lib::unused(node, name);
                return std::unexpected(vfs::error::todo);
            }

            bool sync() override { return true; }

            // TODO
            bool unmount(std::shared_ptr<struct vfs::mount>) override { lib::panic("tmpfs::unmount"); return false; }

            ~instance() = default;
        };

        mutable std::list<std::shared_ptr<struct vfs::mount>> mounts;
        auto mount(std::optional<std::shared_ptr<vfs::dentry>>) const -> vfs::expect<std::shared_ptr<struct vfs::mount>> override
        {
            auto instance = lib::make_locked<fs::instance, lib::mutex>();
            auto root = std::make_shared<vfs::dentry>();
            root->name = "tmpfs root";
            root->inode = std::make_shared<inode>(instance.lock()->inode_num++, static_cast<mode_t>(stat::type::s_ifdir), ops::singleton());

            auto mount = std::make_shared<struct vfs::mount>(std::move(instance), root, std::nullopt);
            mounts.push_back(mount);
            return mount;
        }

        fs() : vfs::filesystem { "tmpfs" } { }
    };

    std::unique_ptr<vfs::filesystem> init()
    {
        static bool once_flag = false;
        if (once_flag)
            lib::panic("tmpfs: tried to initialise twice");

        once_flag = true;
        return std::unique_ptr<vfs::filesystem> { new fs { } };
    }
} // namespace fs::tmpfs

namespace fs::devtmpfs
{
    struct fs : vfs::filesystem
    {
        lib::locked_ptr<tmpfs::fs::instance, lib::mutex> instance;
        std::shared_ptr<vfs::dentry> root;
        mutable std::list<std::shared_ptr<struct vfs::mount>> mounts;

        auto mount(std::optional<std::shared_ptr<vfs::dentry>>) const -> vfs::expect<std::shared_ptr<struct vfs::mount>> override
        {
            auto mount = std::make_shared<struct vfs::mount>(instance, root, std::nullopt);
            mounts.push_back(mount);
            return mount;
        }

        fs() : vfs::filesystem { "devtmpfs" }
        {
            instance = lib::make_locked<tmpfs::fs::instance, lib::mutex>();
            root = std::make_shared<vfs::dentry>();
            root->name = "devtmpfs root. this shouldn't be visible anywhere";
            root->inode = std::make_shared<tmpfs::inode>(instance.lock()->inode_num++, static_cast<mode_t>(stat::type::s_ifdir), tmpfs::ops::singleton());
        }
    };

    std::unique_ptr<vfs::filesystem> init()
    {
        static bool once_flag = false;
        if (once_flag)
            lib::panic("devtmpfs: tried to initialise twice");

        once_flag = true;
        return std::unique_ptr<vfs::filesystem> { new fs };
    }
} // namespace fs::devtmpfs