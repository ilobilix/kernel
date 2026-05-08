// Copyright (C) 2024-2026  ilobilo

module drivers.fs.tmpfs;

import system.memory.phys;
import system.sched.mutex;
import system.sched;
import system.chrono;
import system.vfs.dev;
import fmt;

namespace fs::tmpfs
{
    namespace
    {
        std::size_t page_charge(std::size_t bytes)
        {
            return lib::div_roundup(bytes, pmm::page_size) * pmm::page_size;
        }

        bool reserve(std::atomic<std::size_t> &counter, std::size_t max, std::size_t delta)
        {
            auto cur = counter.load(std::memory_order_relaxed);
            do {
                if (cur + delta > max)
                    return false;
            } while (!counter.compare_exchange_weak(
                cur, cur + delta, std::memory_order_relaxed));
            return true;
        }
    }

    inode::inode(fs::instance *owner, dev_t dev, dev_t rdev, ino_t ino, mode_t mode)
        : vfs::inode { }, owner { owner }, memory { new vmm::memobject { vmm::object_type::shmem } }
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

    inode::~inode()
    {
        if (owner)
        {
            owner->current_inodes.fetch_sub(1, std::memory_order_relaxed);
            owner->current_size.fetch_sub(
                page_charge(static_cast<std::size_t>(stat.st_size)),
                std::memory_order_relaxed
            );
        }
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
        const auto old_size = static_cast<std::size_t>(inod->stat.st_size);
        const auto new_end = offset + size;
        const bool grew = new_end > old_size;

        if (grew)
        {
            const auto growth = page_charge(new_end) - page_charge(old_size);
            if (growth > 0 && !reserve(inod->owner->current_size, inod->owner->max_size, growth))
                return std::unexpected { lib::err::no_space_left };
        }

        const auto ret = inod->memory->write(offset, buffer.subspan(0, size));

        if (grew)
        {
            inod->stat.st_size = new_end;
            inod->stat.st_blocks = lib::div_roundup(
                new_end, static_cast<std::size_t>(inod->stat.st_blksize)
            );
        }
        return ret;
    }

    lib::expect<void> ops::trunc(std::shared_ptr<vfs::file> file, std::size_t size)
    {
        auto inod = reinterpret_cast<inode *>(file->path.dentry->inode.get());
        const std::unique_lock _ { inod->lock };

        const auto old_size = static_cast<std::size_t>(inod->stat.st_size);
        if (size == old_size)
            return { };

        const auto old_charge = page_charge(old_size);
        const auto new_charge = page_charge(size);

        if (size > old_size)
        {
            if (new_charge > old_charge)
            {
                const auto growth = new_charge - old_charge;
                if (!reserve(inod->owner->current_size, inod->owner->max_size, growth))
                    return std::unexpected { lib::err::no_space_left };
            }
            inod->memory->clear(old_size, 0, size - old_size);
        }
        else
        {
            if (old_charge > new_charge)
            {
                inod->owner->current_size.fetch_sub(
                    old_charge - new_charge, std::memory_order_relaxed
                );
            }
            inod->memory->clear(size, 0, old_size - size);
        }

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
        if (!reserve(current_inodes, max_inodes, 1))
            return std::unexpected { lib::err::no_space_left };
        return std::shared_ptr<vfs::inode>(new inode { this, dev_id, rdev, next_inode++, mode });
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

    auto fs::instance::rename(
        std::shared_ptr<vfs::inode> &old_parent, std::string_view old_name,
        std::shared_ptr<vfs::inode> &new_parent, std::string_view new_name,
        std::shared_ptr<vfs::inode> replaced
    ) -> lib::expect<void>
    {
        lib::unused(old_parent, old_name, new_parent, new_name);
        if (replaced)
            replaced->stat.st_nlink--;
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
        lib::unused(inode);
        return { };
    }

    auto fs::instance::dirty_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void>
    {
        lib::unused(inode);
        return { };
    }

    bool fs::instance::sync() { return true; }

    bool fs::instance::unmount(std::shared_ptr<struct vfs::mount>)
    {
        lib::panic("todo: tmpfs::unmount");
        return false;
    }

    std::string fs::instance::mount_options() const
    {
        constexpr auto none = std::numeric_limits<std::size_t>::max();
        constexpr mode_t default_mode = 0777 | s_isvtx;

        std::string out;
        const auto sep = [&] {
            if (!out.empty())
                out.append(",");
        };

        if (max_size != none)
        {
            sep();
            out.append(fmt::format("size={}k", max_size / 1024));
        }
        if (max_inodes != none)
        {
            sep();
            out.append(fmt::format("nr_inodes={}", max_inodes));
        }
        if (opt_mode != default_mode)
        {
            sep();
            out.append(fmt::format("mode={:03o}", opt_mode));
        }
        if (opt_uid != 0)
        {
            sep();
            out.append(fmt::format("uid={}", opt_uid));
        }
        if (opt_gid != 0)
        {
            sep();
            out.append(fmt::format("gid={}", opt_gid));
        }
        return out;
    }

    void fs::instance::statfs(struct ::statfs &out)
    {
        vfs::filesystem::instance::statfs(out);

        const auto cur_size = current_size.load(std::memory_order_relaxed);
        const auto cur_inodes = current_inodes.load(std::memory_order_relaxed);

        const auto blocks = max_size / pmm::page_size;
        const auto used_blocks = (cur_size + pmm::page_size - 1) / pmm::page_size;

        out.f_bsize = pmm::page_size;
        out.f_frsize = pmm::page_size;
        out.f_blocks = blocks;
        out.f_bfree = blocks > used_blocks ? blocks - used_blocks : 0;
        out.f_bavail = out.f_bfree;
        out.f_files = max_inodes;
        out.f_ffree = max_inodes > cur_inodes ? max_inodes - cur_inodes : 0;
    }

    auto fs::mount(
        std::shared_ptr<vfs::dentry> src,
        std::optional<lib::maybe_uspan<const std::byte>> data
    ) const -> lib::expect<std::shared_ptr<struct vfs::mount>>
    {
        lib::unused(src);

        const auto mem = pmm::info().usable;
        lib::kvargs args {
            lib::kvarg_size<std::size_t, "size", true> { mem },
            lib::kvarg_size<std::size_t, "nr_blocks", false> { },
            lib::kvarg_size<std::size_t, "nr_inodes", false> { 0, mem / (pmm::page_size * 2) },
            lib::kvarg<mode_t, "mode"> { 8, 0777 | s_isvtx },
            lib::kvarg<gid_t, "gid"> { 10, 0 },
            lib::kvarg<uid_t, "uid"> { 10, 0 },
        };

        if (data)
        {
            const auto data_size = std::min(data->size(), pmm::page_size);
            if (data->is_user())
            {
                std::string str;
                str.resize(data_size);
                const auto ret = data->subspan(0, data_size).copy_to(
                    reinterpret_cast<std::byte *>(str.data())
                );
                if (!ret)
                    return std::unexpected { lib::err::invalid_address };
                args.parse(str, ',');
            }
            else
            {
                const auto bytes = data->subspan(0, data_size);
                args.parse({
                    reinterpret_cast<const char *>(bytes.span().data()),
                    bytes.size()
                }, ',');
            }
        }

        auto instance = lib::make_locked<fs::instance, sched::mutex>();
        auto locked = instance.lock();
        locked->fs = const_cast<fs *>(this);
        {
            constexpr auto max = std::numeric_limits<std::size_t>::max();

            const auto size = args.get<"size">();
            if (!size.has_value())
            {
                const auto nr_blocks = args.get<"nr_blocks">();
                if (nr_blocks.has_value())
                {
                    const auto blocks = nr_blocks.value();
                    locked->max_size = blocks > max / pmm::page_size ? max : blocks * pmm::page_size;
                }
                else locked->max_size = mem / 2;
            }
            else locked->max_size = size.value();

            if (locked->max_size == 0)
                locked->max_size = max;

            locked->max_inodes = args.get<"nr_inodes">().value() ?: max;
            locked->opt_mode = args.get<"mode">().value() & (0777 | s_isvtx | s_isgid | s_isuid);
            locked->opt_uid = args.get<"uid">().value();
            locked->opt_gid = args.get<"gid">().value();
        }

        auto root = std::make_shared<vfs::dentry>();
        root->name = "tmpfs root. this shouldn't be visible anywhere";
        locked->current_inodes.fetch_add(1, std::memory_order_relaxed);
        root->inode = std::make_shared<inode>(
            locked.get(), locked->dev_id, 0, locked->next_inode++,
            static_cast<mode_t>(stat::type::s_ifdir) | locked->opt_mode
        );
        root->inode->stat.st_uid = locked->opt_uid;
        root->inode->stat.st_gid = locked->opt_gid;
        root->parent = root;

        vfs::dev::register_fs_ops(locked->dev_id, ops::singleton());

        auto mount = std::make_shared<struct vfs::mount>(std::move(instance), root, std::nullopt);
        mounts.push_back(mount);
        return mount;
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
            lib::bug_on(!vfs::register_fs(std::unique_ptr<vfs::filesystem> { new fs { } }));
        }
    };
} // namespace fs::tmpfs
