// Copyright (C) 2024-2025  ilobilo

module drivers.fs.tmpfs;

import system.memory;
import system.vfs;
import lib;
import cppstd;

namespace fs::tmpfs
{
    struct backing : vfs::backing
    {
        struct ops : vfs::backing::ops
        {
            static std::shared_ptr<ops> singleton()
            {
                static auto instance = std::make_shared<ops>();
                return instance;
            }

            std::ssize_t read(std::shared_ptr<vfs::backing> self, std::size_t offset, std::span<std::byte> buffer) override
            {
                auto back = reinterpret_cast<backing *>(self.get());
                const std::unique_lock _ { back->lock };

                auto size = buffer.size_bytes();
                auto real_size = size;
                if (offset + size >= static_cast<std::size_t>(back->stat.st_size))
                    real_size = size - ((offset + size) - back->stat.st_size);

                std::memcpy(buffer.data(), back->data.data() + offset, real_size);
                return real_size;
            }

            std::ssize_t write(std::shared_ptr<vfs::backing> self, std::size_t offset, std::span<std::byte> buffer) override
            {
                auto back = reinterpret_cast<backing *>(self.get());
                const std::unique_lock _ { back->lock };

                auto size = buffer.size_bytes();
                back->data.resize(std::max(back->data.size(), offset + size));
                std::memcpy(back->data.data() + offset, buffer.data(), size);

                if (offset + size >= static_cast<std::size_t>(back->stat.st_size))
                {
                    back->stat.st_size = offset + size;
                    back->stat.st_blocks = lib::div_roundup(offset + size, static_cast<std::size_t>(back->stat.st_blksize));
                }
                return size;
            }

            std::uintptr_t mmap(std::shared_ptr<vfs::backing> self, std::uintptr_t page, int flags) override
            {
                auto back = reinterpret_cast<backing *>(self.get());
                const std::unique_lock _ { back->lock };

                lib::ensure(page * pmm::page_size < back->data.size());

                if (flags & vmm::map::shared)
                    return lib::fromhh(reinterpret_cast<std::uintptr_t>(back->data.data()) + (page * pmm::page_size));

                auto copy = pmm::alloc();
                std::memcpy(lib::tohh(copy), back->data.data() + (page * pmm::page_size), std::min(pmm::page_size, back->data.size()));
                return reinterpret_cast<std::uintptr_t>(copy);
            }

            bool munmap(std::shared_ptr<vfs::backing> self, std::uintptr_t page) override
            {
                lib::unused(self, page);
                return false;
            }
        };

        std::vector<std::byte> data;

        backing(ino_t ino, mode_t mode, std::shared_ptr<ops> op) : vfs::backing { op }
        {
            can_mmap = true;

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

        backing(ino_t ino, mode_t mode) : backing { ino, mode, ops::singleton() } { }
    };

    struct fs : vfs::filesystem
    {
        struct instance : vfs::filesystem::instance, std::enable_shared_from_this<instance>
        {
            std::atomic<ino_t> inode = 0;

            auto create(std::shared_ptr<vfs::node> parent, std::string_view name, mode_t mode) -> vfs::expect<std::shared_ptr<vfs::node>> override
            {
                auto node = std::make_shared<vfs::node>();
                node->parent = parent;
                node->name = name;
                node->backing = std::shared_ptr<vfs::backing>(new backing { inode++, mode });
                return node;
            }

            auto mknod(std::shared_ptr<vfs::node> parent, std::string_view name, mode_t mode, dev_t dev) -> vfs::expect<std::shared_ptr<vfs::node>> override
            {
                lib::panic("TODO: some sort of device registry");
                lib::unused(dev);

                auto node = std::make_shared<vfs::node>();
                node->parent = parent;
                node->name = name;
                node->backing = std::shared_ptr<vfs::backing>(new backing { inode++, mode });
                return node;
            }

            auto symlink(std::shared_ptr<vfs::node> parent, std::string_view name, lib::path target) -> vfs::expect<std::shared_ptr<vfs::node>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected(vfs::error::todo);
            }

            auto link(std::shared_ptr<vfs::node> parent, std::string_view name, std::shared_ptr<vfs::node> target) -> vfs::expect<std::shared_ptr<vfs::node>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected(vfs::error::todo);
            }

            auto unlink(std::shared_ptr<vfs::node> node) -> vfs::expect<void> override
            {
                lib::unused(node);
                return std::unexpected(vfs::error::todo);
            }

            bool populate(std::shared_ptr<vfs::node> node, std::string_view name = "") override
            {
                lib::unused(node, name);
                return false;
            }
            bool sync() override { return true; }
            // TODO
            bool unmount() override { lib::panic("tmpfs::unmount"); return false; }

            ~instance() = default;
        };

        std::pair<std::shared_ptr<vfs::filesystem::instance>, std::shared_ptr<vfs::node>> mount(std::shared_ptr<vfs::node>) const override
        {
            auto instance = std::make_shared<fs::instance>();
            auto root = std::make_shared<vfs::node>();
            root->name = "tmpfs root. this shouldn't be visible anywhere";

            instance->root = root;
            root->backing = std::make_shared<backing>(instance->inode++, static_cast<mode_t>(stat::type::s_ifdir));
            root->fs = instance;

            return { instance, root };
        }

        fs() : vfs::filesystem { "tmpfs" } { }
    };

    std::unique_ptr<vfs::filesystem> init()
    {
        static bool once_flag = false;
        if (once_flag)
            lib::panic("tmpfs: tried to initialise twice");

        once_flag = true;
        return std::unique_ptr<vfs::filesystem> { new fs };
    }
} // namespace fs::tmpfs

namespace fs::devtmpfs
{
    struct fs : vfs::filesystem
    {
        std::shared_ptr<vfs::node> _root;

        std::pair<std::shared_ptr<vfs::filesystem::instance>, std::shared_ptr<vfs::node>> mount(std::shared_ptr<vfs::node>) const override
        {
            auto instance = std::make_shared<tmpfs::fs::instance>();
            auto root = std::make_shared<vfs::node>();
            root->name = "devtmpfs instance root. this shouldn't be visible anywhere";
            root->children_redirect = _root;

            instance->root = root;
            root->backing = std::make_shared<tmpfs::backing>(instance->inode++, static_cast<mode_t>(stat::type::s_ifdir));
            root->fs = instance;

            return { instance, root };
        }

        fs() : vfs::filesystem { "devtmpfs" }
        {
            _root = std::make_shared<vfs::node>();
            _root->name = "devtmpfs main root. this shouldn't be visible anywhere";
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