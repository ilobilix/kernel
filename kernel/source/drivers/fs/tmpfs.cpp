// Copyright (C) 2024-2026  ilobilo

module drivers.fs.tmpfs;

import system.memory.virt;
import system.sched.mutex;
import system.sched;
import system.chrono;
import system.vfs.dev;
import system.vfs;
import lib;
import std;

namespace fs::tmpfs
{
    inode::inode(dev_t dev, dev_t rdev, ino_t ino, mode_t mode)
        : vfs::inode { }, memory { new vmm::memobject { } }
    {
        stat.st_size = 0;
        stat.st_blocks = 0;
        stat.st_blksize = 512;

        stat.st_dev = dev;
        stat.st_rdev = rdev;

        stat.st_ino = ino;
        stat.st_mode = mode;
        stat.st_nlink = 1;

        const auto proc = sched::current_process();
        stat.st_uid = proc->cred->euid;
        stat.st_gid = proc->cred->egid;

        stat.update_time(
            kstat::time::access |
            kstat::time::modify |
            kstat::time::status |
            kstat::time::birth
        );
    }

    lib::expect<std::size_t> ops::read(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        auto size = buffer.size_bytes();

        const auto file_size = static_cast<std::size_t>(inod->stat.st_size);
        if (offset >= file_size)
            return 0;

        const auto real_size = std::min(size, file_size - offset);
        if (real_size == 0)
            return 0;

        return inod->memory->read(offset, buffer.subspan(0, real_size));
    }

    lib::expect<std::size_t> ops::write(
        std::shared_ptr<vfs::file> file, std::uint64_t offset,
        lib::maybe_uspan<std::byte> buffer
    )
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        if (file->flags & vfs::o_append)
        {
            offset = static_cast<std::size_t>(inod->stat.st_size);
            file->offset = offset;
        }

        const auto size = buffer.size_bytes();
        const auto ret = inod->memory->write(offset, buffer.subspan(0, size));

        if (offset + size >= static_cast<std::size_t>(inod->stat.st_size))
        {
            inod->stat.st_size = offset + size;
            inod->stat.st_blocks = lib::div_roundup(
                offset + size, static_cast<std::size_t>(inod->stat.st_blksize)
            );
        }
        return ret;
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
        inod->stat.st_blocks = lib::div_roundup(
            size, static_cast<std::size_t>(inod->stat.st_blksize)
        );
        return { };
    }

    lib::expect<vmm::object::ptr> ops::map(std::shared_ptr<vfs::file> file)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        return inod->memory;
    }

    auto fs::instance::create(
        std::shared_ptr<vfs::inode> &parent,
        std::string_view name, mode_t mode, dev_t rdev
    ) -> lib::expect<std::shared_ptr<vfs::inode>>
    {
        lib::unused(parent, name);
        return std::shared_ptr<vfs::inode>(new inode { dev_id, rdev, next_inode++, mode });
    }

    auto fs::instance::symlink(
        std::shared_ptr<vfs::inode> &parent,
        std::string_view name, lib::path target
    ) -> lib::expect<std::shared_ptr<vfs::inode>>
    {
        lib::unused(target);
        return create(parent, name, static_cast<mode_t>(stat::type::s_iflnk), 0);
    }

    auto fs::instance::link(
        std::shared_ptr<vfs::inode> &parent,
        std::string_view name, std::shared_ptr<vfs::inode> target
    ) -> lib::expect<std::shared_ptr<vfs::inode>>
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

    auto fs::instance::readdir(std::shared_ptr<vfs::dentry> dir, std::size_t cookie)
        -> lib::expect<lib::list<vfs::dir_entry>>
    {
        constexpr std::size_t max_batch = 256;
        lib::list<vfs::dir_entry> result;

        const auto locked = dir->children.lock();
        std::size_t progress = 0;
        for (auto it = locked->begin_at(cookie); it != locked->end(); it++, progress++)
        {
            if (progress >= max_batch)
                break;

            result.push_back({
                it->dentry->name,
                it->dentry->inode,
                it->cookie
            });
        }
        return result;
    }

    auto fs::instance::lookup(std::shared_ptr<vfs::dentry> dir,std::string_view name)
        -> lib::expect<std::optional<vfs::dir_entry>>
    {
        const auto locked = dir->children.lock();
        if (auto den = locked->lookup(name); den != nullptr)
            return vfs::dir_entry { std::string { name }, den->inode, 0 };
        return std::nullopt;
    }

    auto fs::instance::write_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void>
    {
        inode->dirty = false;
        return { };
    }

    auto fs::instance::dirty_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void>
    {
        inode->dirty = true;
        return { };
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

        auto instance = lib::make_locked<fs::instance, sched::mutex>();
        auto locked = instance.lock();
        const auto dev_id = locked->dev_id;

        auto root = std::make_shared<vfs::dentry>();
        root->name = "tmpfs root. this shouldn't be visible anywhere";
        root->inode = std::make_shared<inode>(
            dev_id, 0, locked->next_inode++,
            static_cast<mode_t>(stat::type::s_ifdir) |
                (s_irwxu | s_irgrp | s_ixgrp | s_iroth | s_ixoth)
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
