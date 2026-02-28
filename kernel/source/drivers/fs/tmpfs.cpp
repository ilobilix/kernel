// Copyright (C) 2024-2026  ilobilo

module drivers.fs.tmpfs;

import system.memory.virt;
import system.scheduler;
import system.chrono;
import system.vfs.dev;
import system.vfs;
import lib;
import std;

namespace fs::tmpfs
{
    inode::inode(dev_t dev, dev_t rdev, ino_t ino, mode_t mode)
        : vfs::inode { }, memory { std::make_shared<vmm::memobject>() }
    {
        stat.st_size = 0;
        stat.st_blocks = 0;
        stat.st_blksize = 512;

        stat.st_dev = dev;
        stat.st_rdev = rdev;

        stat.st_ino = ino;
        stat.st_mode = mode;
        stat.st_nlink = 1;

        const auto thread = sched::this_thread();
        const auto proc = thread->parent;

        stat.st_uid = proc->euid;
        stat.st_gid = proc->egid;

        stat.update_time(
            stat::time::access |
            stat::time::modify |
            stat::time::status
        );
    }

    lib::expect<std::size_t> ops::read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        auto size = buffer.size_bytes();
        auto real_size = size;
        if (offset + size >= static_cast<std::size_t>(inod->stat.st_size))
            real_size = size - ((offset + size) - inod->stat.st_size);

        if (real_size == 0)
            return 0;

        inod->memory->read(offset, buffer.subspan(0, real_size));
        return real_size;
    }

    lib::expect<std::size_t> ops::write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        auto size = buffer.size_bytes();
        inod->memory->write(offset, buffer.subspan(0, size));

        if (offset + size >= static_cast<std::size_t>(inod->stat.st_size))
        {
            inod->stat.st_size = offset + size;
            inod->stat.st_blocks = lib::div_roundup(offset + size, static_cast<std::size_t>(inod->stat.st_blksize));
        }
        return size;
    }

    lib::expect<void> ops::trunc(std::shared_ptr<vfs::file> file, std::size_t size)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        const auto current_size = static_cast<std::size_t>(inod->stat.st_size);
        if (size == current_size)
            return { };

        if (size < current_size)
            inod->memory->clear(size, 0, current_size - size);
        else
            inod->memory->clear(current_size, 0, size - current_size);

        inod->stat.st_size = size;
        inod->stat.st_blocks = lib::div_roundup(size, static_cast<std::size_t>(inod->stat.st_blksize));
        return { };
    }

    lib::expect<std::size_t> ops::getdents(std::shared_ptr<vfs::file> file, std::uint64_t &offset, lib::maybe_uspan<std::byte> buffer)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        if (inod->stat.type() != stat::type::s_ifdir)
            return std::unexpected { lib::err::not_a_dir };

        auto rlocked = file->path.dentry->children.read_lock();

        std::size_t progress = 0;
        for (auto it = rlocked->begin_at(offset); it != rlocked->end(); it++)
        {
            const auto &[dentry, cookie] = *it;
            const auto reclen = (sizeof(vfs::dirent64) + dentry->name.length() + 1 + 7) & ~7;

            if (reclen + progress > buffer.size())
            {
                if (progress == 0)
                    return std::unexpected { lib::err::buffer_too_small };
                break;
            }

            const auto &stat = dentry->inode->stat;

            offset = cookie + 1;
            vfs::dirent64 dirent
            {
                .d_ino = stat.st_ino,
                .d_off = static_cast<std::int64_t>(offset),
                .d_reclen = static_cast<std::uint16_t>(reclen),
                .d_type = vfs::stat_to_dt(stat.type())
            };

            buffer.subspan(progress, sizeof(vfs::dirent64))
                .copy_from(reinterpret_cast<std::byte *>(&dirent));

            const std::span name {
                reinterpret_cast<std::byte *>(dentry->name.data()),
                dentry->name.length() + 1
            };
            buffer.subspan(progress + sizeof(vfs::dirent64), dentry->name.length() + 1)
                .copy_from(name);

            progress += reclen;
        }
        return progress;
    }

    lib::expect<std::shared_ptr<vmm::object>> ops::map(std::shared_ptr<vfs::file> file, bool priv)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        if (priv)
        {
            auto memory = std::make_shared<vmm::memobject>();
            inod->memory->copy_to(*memory, 0, static_cast<std::size_t>(inod->stat.st_size));
            return memory;
        }
        return inod->memory;
    }

    auto fs::instance::create(std::shared_ptr<vfs::inode> &parent, std::string_view name, mode_t mode, dev_t rdev) -> lib::expect<std::shared_ptr<vfs::inode>>
    {
        lib::unused(parent, name);
        return std::shared_ptr<vfs::inode>(new inode { dev_id, rdev, next_inode++, mode });
    }

    auto fs::instance::symlink(std::shared_ptr<vfs::inode> &parent, std::string_view name, lib::path target) -> lib::expect<std::shared_ptr<vfs::inode>>
    {
        lib::unused(target);
        return create(parent, name, static_cast<mode_t>(stat::type::s_iflnk), 0);
    }

    auto fs::instance::link(std::shared_ptr<vfs::inode> &parent, std::string_view name, std::shared_ptr<vfs::inode> target) -> lib::expect<std::shared_ptr<vfs::inode>>
    {
        lib::unused(parent, name);
        target->stat.st_nlink++;
        return target;
    }

    auto fs::instance::unlink(std::shared_ptr<vfs::inode> &node) -> lib::expect<void>
    {
        node->stat.st_nlink--;
        return { };
    }

    auto fs::instance::populate(std::shared_ptr<vfs::inode> &node, std::string_view name) -> lib::expect<lib::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>>
    {
        lib::unused(node, name);
        return std::unexpected { lib::err::todo };
    }

    bool fs::instance::sync() { return true; }

    bool fs::instance::unmount(std::shared_ptr<struct vfs::mount>)
    {
        lib::panic("todo: tmpfs::unmount");
        return false;
    }

    auto fs::mount(std::shared_ptr<vfs::dentry> src) const -> lib::expect<std::shared_ptr<struct vfs::mount>>
    {
        lib::unused(src);

        auto instance = lib::make_locked<fs::instance, lib::mutex>();
        auto locked = instance.lock();
        const auto dev_id = locked->dev_id;

        auto root = std::make_shared<vfs::dentry>();
        root->name = "tmpfs root. this shouldn't be visible anywhere";
        root->inode = std::make_shared<inode>(
            dev_id, 0, locked->next_inode++,
            static_cast<mode_t>(stat::type::s_ifdir)
        );
        root->parent = root;

        vfs::dev::register_fs_ops(dev_id, ops::singleton());

        auto mount = std::make_shared<struct vfs::mount>(std::move(instance), root, std::nullopt);
        mounts.push_back(mount);
        return mount;
    }

    std::unique_ptr<vfs::filesystem> init()
    {
        static bool once_flag = false;
        if (once_flag)
            lib::panic("tmpfs: tried to initialise twice");

        once_flag = true;
        return std::unique_ptr<vfs::filesystem> { new fs { } };
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.tmpfs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task tmpfs_task
    {
        "vfs.tmpfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { registered_stage() },
        [] {
            lib::bug_on(!vfs::register_fs(tmpfs::init()));
        }
    };
} // namespace fs::tmpfs