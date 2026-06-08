// Copyright (C) 2024-2026  ilobilo

module drivers.fs.sysfs;

import system.memory.virt;
import system.sched;
import system.vfs.dev;
import system.vfs;
import system.dev;
import std;

namespace fs::sysfs
{
    struct inode : vfs::inode
    {
        enum class type
        {
            dir,
            attr,
            bin,
            uevent,
            symlink
        };

        type typ;
        std::shared_ptr<dev::kobject_t> kobj;
        dev::attribute_t *attr;
        dev::bin_attribute_t *battr;

        inode(
            type typ, std::shared_ptr<dev::kobject_t> kobj, dev::attribute_t *attr,
            dev::bin_attribute_t *battr, dev_t dev, ino_t ino, std::shared_ptr<vfs::ops> iops
        )
            : vfs::inode { iops }, typ { typ }, kobj { kobj }, attr { attr }, battr { battr }
        {
            stat.st_dev = dev;
            stat.st_rdev = 0;
            stat.st_ino = ino;
            stat.st_size = 0;

            switch (typ)
            {
                case type::dir:
                    stat.st_mode = stat::s_ifdir | 0755;
                    break;
                case type::attr:
                    lib::bug_on(!attr);
                    stat.st_mode = stat::s_ifreg | attr->mode;
                    break;
                case type::bin:
                    lib::bug_on(!battr);
                    stat.st_mode = stat::s_ifreg | battr->mode;
                    stat.st_size = battr->size(*kobj);
                    break;
                case type::uevent:
                    stat.st_mode = stat::s_ifreg | (kobj->as_device() ? 0644 : 0200);
                    break;
                case type::symlink:
                    stat.st_mode = stat::s_iflnk | 0777;
                    break;
            }

            stat.st_nlink = 1;
            stat.st_uid = 0;
            stat.st_gid = 0;
            stat.st_blksize = 4096;
            stat.st_blocks = 0;

            stat.update_time(
                kstat::time::access | kstat::time::modify | kstat::time::status | kstat::time::birth
            );
        }
    };

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
        ) override
        {
            const auto inod = std::static_pointer_cast<inode>(file->path.dentry->inode);

            if (inod->typ == inode::type::bin)
            {
                const auto total = inod->battr->size(*inod->kobj);
                if (offset >= total)
                    return 0uz;

                const auto count = std::min(buffer.size(), total - offset);
                if (count == 0)
                    return 0uz;

                lib::membuffer buf { count };
                const auto ret = inod->battr->read(*inod->kobj, buf.span(), offset);
                if (!ret)
                    return std::unexpected { ret.error() };

                const auto sub = buffer.subspan(0, *ret);
                if (!sub.copy_from(buf.span()))
                    return std::unexpected { lib::err::invalid_address };
                return *ret;
            }

            if (!file->private_data || offset == 0)
            {
                if (inod->typ == inode::type::attr)
                {
                    auto content = inod->attr->show(*inod->kobj);
                    if (!content)
                        return std::unexpected { content.error() };

                    file->private_data =
                        std::shared_ptr<std::string> { new std::string { std::move(*content) } };
                }
                else if (inod->typ == inode::type::uevent)
                {
                    file->private_data =
                        std::shared_ptr<std::string> { new std::string { inod->kobj->uevent_text() } };
                }
                else
                    return std::unexpected { lib::err::io_error };
            }

            auto content = std::static_pointer_cast<std::string>(file->private_data);
            if (offset >= content->size())
                return 0uz;

            const auto remaining = content->size() - offset;
            const auto to_copy = std::min(buffer.size(), remaining);
            if (to_copy == 0)
                return 0uz;

            const auto sub = buffer.subspan(0, to_copy);
            if (!sub.copy_from(reinterpret_cast<const std::byte *>(content->data() + offset)))
                return std::unexpected { lib::err::invalid_address };

            return to_copy;
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override
        {
            const auto inod = std::static_pointer_cast<inode>(file->path.dentry->inode);

            if (inod->typ == inode::type::bin)
            {
                std::vector<std::byte> data(buffer.size());
                if (!buffer.copy_to(data.data()))
                    return std::unexpected { lib::err::invalid_address };

                const auto ret = inod->battr->write(*inod->kobj, data, offset);
                if (!ret)
                    return std::unexpected { ret.error() };
                return *ret;
            }
            else if (inod->typ == inode::type::uevent)
            {
                std::string data(buffer.size(), '\0');
                if (!buffer.copy_to(reinterpret_cast<std::byte *>(data.data())))
                    return std::unexpected { lib::err::invalid_address };

                if (const auto ret = dev::uevent_store(*inod->kobj, data); !ret)
                    return std::unexpected { ret.error() };
                return buffer.size();
            }
            else if (inod->typ != inode::type::attr)
                return std::unexpected { lib::err::invalid_argument };

            std::string data(buffer.size(), '\0');
            if (!buffer.copy_to(reinterpret_cast<std::byte *>(data.data())))
                return std::unexpected { lib::err::invalid_address };

            if (const auto ret = inod->attr->store(*inod->kobj, data); !ret)
                return std::unexpected { ret.error() };

            file->private_data.reset();
            return buffer.size();
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(size);
            const auto inod = std::static_pointer_cast<inode>(file->path.dentry->inode);
            if (inod->typ != inode::type::attr && inod->typ != inode::type::bin &&
                inod->typ != inode::type::uevent)
                return std::unexpected { lib::err::read_only_fs };
            return { };
        }

        lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file> file) override
        {
            const auto inod = std::static_pointer_cast<inode>(file->path.dentry->inode);
            if (inod->typ != inode::type::bin)
                return std::unexpected { lib::err::mapping_unsupported };
            return inod->battr->mmap(*inod->kobj);
        }

        lib::expect<void> getattr(std::shared_ptr<vfs::inode> node) override
        {
            const auto inod = std::static_pointer_cast<inode>(node);
            if (inod->typ == inode::type::bin)
            {
                const std::unique_lock _ { inod->lock };
                inod->stat.st_size = inod->battr->size(*inod->kobj);
            }
            return { };
        }
    };

    struct fs : vfs::filesystem
    {
        struct instance : vfs::filesystem::instance,
                          dev::reflector_t,
                          std::enable_shared_from_this<instance>
        {
            // clang-format off
            lib::locker<
                lib::map::flat_hash<
                    dev::kobject_t *,
                    std::shared_ptr<vfs::dentry>
                >, sched::mutex
            > nodes;
            // clang-format on

            auto create(
                std::shared_ptr<vfs::inode> &parent, std::string_view name, mode_t mode, dev_t rdev,
                std::optional<std::shared_ptr<vfs::ops>> ops
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name, rdev, ops);
                if (stat::type(mode) == stat::s_ifreg)
                    return std::unexpected { lib::err::permission_denied };
                return std::unexpected { lib::err::not_permitted };
            }

            auto symlink(
                std::shared_ptr<vfs::inode> &parent, std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::not_permitted };
            }

            auto link(
                std::shared_ptr<vfs::inode> &parent, std::string_view name,
                std::shared_ptr<vfs::inode> target
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::not_permitted };
            }

            auto unlink(std::shared_ptr<vfs::inode> &node) -> lib::expect<void> override
            {
                lib::unused(node);
                return std::unexpected { lib::err::not_permitted };
            }

            auto rename(
                std::shared_ptr<vfs::inode> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode> replaced
            ) -> lib::expect<void> override
            {
                lib::unused(old_parent, old_name, new_parent, new_name, replaced);
                return std::unexpected { lib::err::not_permitted };
            }

            auto readdir(std::shared_ptr<vfs::dentry> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override
            {
                constexpr std::size_t max_batch = 256;
                lib::list<vfs::dir_entry> result;

                const auto locked = dir->children.lock();
                std::size_t progress = 0;
                for (auto it = locked->begin_at(cookie); it != locked->end(); it++, progress++)
                {
                    if (progress >= max_batch)
                        break;

                    result.push_back({ it->dentry->name, it->dentry->inode, it->cookie });
                }
                return result;
            }

            auto lookup(std::shared_ptr<vfs::dentry> dir, std::string_view name)
                -> lib::expect<vfs::dir_entry> override
            {
                const auto locked = dir->children.lock();
                if (auto den = locked->lookup(name); den != nullptr)
                    return vfs::dir_entry { std::string { name }, den->inode, 0 };
                return std::unexpected { lib::err::not_found };
            }

            auto write_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void> override
            {
                lib::unused(inode);
                return { };
            }

            auto dirty_inode(std::shared_ptr<vfs::inode> &inode) -> lib::expect<void> override
            {
                lib::unused(inode);
                return { };
            }

            bool sync() override { return true; }

            bool unmount(std::shared_ptr<struct vfs::mount> mnt) override
            {
                lib::unused(mnt);
                return false;
            }

            std::shared_ptr<inode> mkdir(const std::shared_ptr<dev::kobject_t> &kobj)
            {
                return std::make_shared<inode>(
                    inode::type::dir, kobj, nullptr, nullptr, dev_id, next_inode++, ops::singleton()
                );
            }

            std::shared_ptr<inode> mksym()
            {
                return std::make_shared<inode>(
                    inode::type::symlink, nullptr, nullptr, nullptr, dev_id, next_inode++,
                    ops::singleton()
                );
            }

            std::shared_ptr<inode> mkuevent(const std::shared_ptr<dev::kobject_t> &kobj)
            {
                return std::make_shared<inode>(
                    inode::type::uevent, kobj, nullptr, nullptr, dev_id, next_inode++,
                    ops::singleton()
                );
            }

            std::shared_ptr<inode> mkattr(
                const std::shared_ptr<dev::kobject_t> &kobj, dev::attribute_t *attr
            )
            {
                return std::make_shared<inode>(
                    inode::type::attr, kobj, attr, nullptr, dev_id, next_inode++, ops::singleton()
                );
            }

            std::shared_ptr<inode> mkbin(
                const std::shared_ptr<dev::kobject_t> &kobj, dev::bin_attribute_t *battr
            )
            {
                return std::make_shared<inode>(
                    inode::type::bin, kobj, nullptr, battr, dev_id, next_inode++, ops::singleton()
                );
            }

            std::shared_ptr<vfs::dentry> get_or_create(const std::shared_ptr<dev::kobject_t> &kobj)
            {
                {
                    auto locked = nodes.lock();
                    if (auto it = locked->find(kobj.get()); it != locked->end())
                        return it->second;
                }

                std::shared_ptr<vfs::dentry> parent;
                if (auto locked = kobj->parent.lock())
                    parent = get_or_create(locked);
                else
                    parent = static_cast<struct fs *>(fs)->root;

                auto locked = nodes.lock();
                if (auto it = locked->find(kobj.get()); it != locked->end())
                    return it->second;

                auto dentry = std::make_shared<vfs::dentry>();
                dentry->name = kobj->name;
                dentry->inode = mkdir(kobj);
                dentry->parent = parent;

                for (const auto &attr : kobj->type->attributes())
                {
                    auto child = std::make_shared<vfs::dentry>();
                    child->name = attr->name;
                    child->inode = mkattr(kobj, attr);
                    child->parent = dentry;
                    dentry->children.lock()->insert(std::move(child));
                }

                for (const auto &battr : kobj->type->bin_attributes())
                {
                    auto child = std::make_shared<vfs::dentry>();
                    child->name = battr->name;
                    child->inode = mkbin(kobj, battr);
                    child->parent = dentry;
                    dentry->children.lock()->insert(std::move(child));
                }

                if (kobj->as_device() || kobj->type != dev::default_ktype())
                {
                    auto child = std::make_shared<vfs::dentry>();
                    child->name = "uevent";
                    child->inode = mkuevent(kobj);
                    child->parent = dentry;
                    dentry->children.lock()->insert(std::move(child));
                }

                parent->children.lock()->insert(dentry);
                locked.value()[kobj.get()] = dentry;
                return dentry;
            }

            void add_object(const std::shared_ptr<dev::kobject_t> &kobj) override
            {
                get_or_create(kobj);
            }

            void remove_object(const std::shared_ptr<dev::kobject_t> &kobj) override
            {
                std::shared_ptr<vfs::dentry> dentry;
                {
                    auto locked = nodes.lock();
                    const auto it = locked->find(kobj.get());
                    if (it == locked->end())
                        return;

                    dentry = it->second;
                    locked->erase(it);
                }

                if (const auto parent = dentry->parent.lock())
                    parent->children.lock()->erase(dentry->name);
            }

            void add_link(
                const std::shared_ptr<dev::kobject_t> &dir, std::string_view name,
                const lib::path &target
            ) override
            {
                auto dentry = get_or_create(dir);
                auto locked = dentry->children.lock();
                if (locked->lookup(name))
                    return;

                auto child = std::make_shared<vfs::dentry>();
                child->name = name;
                child->symlinked_to = target.relative(dir->path());
                child->inode = mksym();
                child->parent = dentry;

                locked->insert(std::move(child));
            }

            void remove_link(
                const std::shared_ptr<dev::kobject_t> &dir, std::string_view name
            ) override
            {
                std::shared_ptr<vfs::dentry> dentry;
                {
                    auto locked = nodes.lock();
                    const auto it = locked->find(dir.get());
                    if (it == locked->end())
                        return;

                    dentry = it->second;
                }

                dentry->children.lock()->erase(name);
            }

            ~instance() = default;
        };

        lib::locked_ptr<instance, sched::mutex> inst;
        std::shared_ptr<vfs::dentry> root;

        std::shared_ptr<struct vfs::mount> internal_mnt;
        mutable lib::locker<lib::list<std::shared_ptr<struct vfs::mount>>, sched::mutex> mounts;

        auto mount(
            std::shared_ptr<vfs::dentry> src, std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount>> override
        {
            lib::unused(src, data);

            auto mount = std::make_shared<struct vfs::mount>(inst, root);
            mounts.lock()->push_back(mount);
            return mount;
        }

        fs() : vfs::filesystem { "sysfs", 0x62656572 }
        {
            inst = lib::make_locked<instance, sched::mutex>();
            auto locked = inst.lock();
            locked->fs = this;

            root = std::make_shared<vfs::dentry>();
            root->name = "sysfs root";
            root->inode = locked->mkdir(nullptr);
            root->parent = root;

            internal_mnt = std::make_shared<struct vfs::mount>(inst, root);
        }
    };

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage {
            "vfs.sysfs.registered", lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *mounted_stage()
    {
        static lib::initgraph::stage stage {
            "vfs.sysfs.mounted", lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task {
        "vfs.sysfs.register", lib::initgraph::postsched_init_engine,
        lib::initgraph::require { dev::core_registered_stage() },
        lib::initgraph::entail { registered_stage() }, [] {
            auto sysfs = std::make_shared<fs>();
            lib::bug_on(!vfs::register_fs(sysfs));
            dev::attach_reflector(sysfs->inst.lock().get());
        }
    };

    lib::initgraph::task mount_task {
        "vfs.sysfs.mount", lib::initgraph::postsched_init_engine,
        lib::initgraph::require { vfs::root_mounted_stage(), registered_stage() },
        lib::initgraph::entail { mounted_stage() }, [] {
            const auto cerr = vfs::create(std::nullopt, "/sys", stat::s_ifdir | 0555);
            if (!cerr && cerr.error() != lib::err::already_exists)
            {
                lib::panic(
                    "sysfs: failed to create directory '/sys': {}", lib::error_name(cerr.error())
                );
            }

            if (const auto merr = vfs::mount(
                    "", "/sys", "sysfs", vfs::ms_nosuid | vfs::ms_nodev | vfs::ms_noexec
                );
                !merr)
            {
                lib::panic(
                    "sysfs: failed to mount sysfs at '/sys': {}", lib::error_name(merr.error())
                );
            }
        }
    };
} // namespace fs::sysfs
