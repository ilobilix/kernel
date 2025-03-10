// Copyright (C) 2024-2025  ilobilo

module drivers.fs.tmpfs;

import system.vfs;
import lib;
import std;

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
                std::unique_lock _ { back->lock };

                auto size = buffer.size_bytes();
                auto real_size = size;
                if (off_t(offset + size) >= back->stat.st_size)
                    real_size = size - ((offset + size) - back->stat.st_size);

                std::memcpy(buffer.data(), back->data.data() + offset, real_size);
                return real_size;
            }

            std::ssize_t write(std::shared_ptr<vfs::backing> self, std::size_t offset, std::span<std::byte> buffer) override
            {
                auto back = reinterpret_cast<backing *>(self.get());
                std::unique_lock _ { back->lock };

                auto size = buffer.size_bytes();
                back->data.resize(std::max(back->data.size(), offset + size));
                std::memcpy(back->data.data() + offset, buffer.data(), size);

                if (off_t(offset + size) >= back->stat.st_size)
                {
                    back->stat.st_size = offset + size;
                    back->stat.st_blocks = lib::div_roundup(offset + size, static_cast<std::size_t>(back->stat.st_blksize));
                }
                return size;
            }
        };

        std::vector<std::byte> data;

        backing(ino_t ino, mode_t mode) : vfs::backing { ops::singleton() }
        {
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

            auto symlink(std::shared_ptr<vfs::node> parent, std::string_view name, lib::path target) -> vfs::expect<std::shared_ptr<vfs::node>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected(vfs::error::todo)    ;
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
            bool unmount() override { return false; }

            ~instance() = default;
        };

        std::pair<std::shared_ptr<vfs::filesystem::instance>, std::shared_ptr<vfs::node>> mount(std::shared_ptr<vfs::node>) const override
        {
            auto instance = std::make_shared<fs::instance>();
            auto root = std::make_shared<vfs::node>();

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