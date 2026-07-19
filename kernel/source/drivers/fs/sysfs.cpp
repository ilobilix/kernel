// Copyright (C) 2024-2026  ilobilo

module drivers.fs.sysfs;

import system.memory.virt;
import system.sched;
import system.vfs.dev;
import system.vfs;
import system.dev;
import frigg;
import std;

namespace fs::sysfs
{
    namespace
    {
        struct inode_t : vfs::inode_t
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

            inode_t(
                type typ, std::shared_ptr<dev::kobject_t> kobj, dev::attribute_t *attr,
                dev::bin_attribute_t *battr, dev_t dev, ino_t ino, std::shared_ptr<vfs::ops_t> iops
            ) : vfs::inode_t { iops }, typ { typ }, kobj { kobj }, attr { attr }, battr { battr }
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
                    kstat::time::access |
                    kstat::time::modify |
                    kstat::time::status |
                    kstat::time::birth
                );
            }
        };

        struct ops_t : vfs::ops_t
        {
            static std::shared_ptr<ops_t> singleton()
            {
                static auto instance = std::make_shared<ops_t>();
                return instance;
            }

            lib::expect<std::size_t> read(
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                const auto inod = std::static_pointer_cast<inode_t>(file->path.dentry->inode);

                if (inod->typ == inode_t::type::bin)
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
                    if (inod->typ == inode_t::type::attr)
                    {
                        auto content = inod->attr->show(*inod->kobj);
                        if (!content)
                            return std::unexpected { content.error() };

                        file->private_data = std::shared_ptr<std::string> {
                            new std::string { std::move(*content) }
                        };
                    }
                    else if (inod->typ == inode_t::type::uevent)
                    {
                        file->private_data = std::shared_ptr<std::string> {
                            new std::string { inod->kobj->uevent_text() }
                        };
                    }
                    else return std::unexpected { lib::err::io_error };
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
                std::shared_ptr<vfs::file_t> file, std::uint64_t offset,
                lib::maybe_uspan<std::byte> buffer
            ) override
            {
                const auto inod = std::static_pointer_cast<inode_t>(file->path.dentry->inode);

                if (inod->typ == inode_t::type::bin)
                {
                    std::vector<std::byte> data(buffer.size());
                    if (!buffer.copy_to(data.data()))
                        return std::unexpected { lib::err::invalid_address };

                    const auto ret = inod->battr->write(*inod->kobj, data, offset);
                    if (!ret)
                        return std::unexpected { ret.error() };
                    return *ret;
                }
                else if (inod->typ == inode_t::type::uevent)
                {
                    std::string data(buffer.size(), '\0');
                    if (!buffer.copy_to(reinterpret_cast<std::byte *>(data.data())))
                        return std::unexpected { lib::err::invalid_address };

                    if (const auto ret = dev::uevent_store(*inod->kobj, data); !ret)
                        return std::unexpected { ret.error() };
                    return buffer.size();
                }
                else if (inod->typ != inode_t::type::attr)
                    return std::unexpected { lib::err::invalid_argument };

                std::string data(buffer.size(), '\0');
                if (!buffer.copy_to(reinterpret_cast<std::byte *>(data.data())))
                    return std::unexpected { lib::err::invalid_address };

                if (const auto ret = inod->attr->store(*inod->kobj, data); !ret)
                    return std::unexpected { ret.error() };

                file->private_data.reset();
                return buffer.size();
            }

            bool truncable() const override { return true; }
            lib::expect<void> trunc(std::shared_ptr<vfs::file_t> file, std::size_t size) override
            {
                lib::unused(size);
                const auto inod = std::static_pointer_cast<inode_t>(file->path.dentry->inode);
                if (inod->typ != inode_t::type::attr && inod->typ != inode_t::type::bin &&
                    inod->typ != inode_t::type::uevent)
                    return std::unexpected { lib::err::read_only_fs };
                return { };
            }

            lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file_t> file) override
            {
                const auto inod = std::static_pointer_cast<inode_t>(file->path.dentry->inode);
                if (inod->typ != inode_t::type::bin)
                    return std::unexpected { lib::err::mapping_unsupported };
                return inod->battr->map(*inod->kobj);
            }

            lib::expect<void> getattr(std::shared_ptr<vfs::inode_t> node) override
            {
                const auto inod = std::static_pointer_cast<inode_t>(node);
                if (inod->typ == inode_t::type::bin)
                {
                    const std::unique_lock _ { inod->lock };
                    inod->stat.st_size = inod->battr->size(*inod->kobj);
                }
                return { };
            }
        };

        struct fs_t : vfs::filesystem_t
        {
            struct instance_t : vfs::filesystem_t::instance_t, dev::reflector_t,
                std::enable_shared_from_this<instance_t>
            {
                lib::locker<
                    lib::map::flat_hash<
                        dev::kobject_t *,
                        std::shared_ptr<vfs::dentry_t>
                    >, sched::mutex
                > nodes;

                auto create(
                    std::shared_ptr<vfs::inode_t> &parent, std::string_view name,
                    mode_t mode, dev_t rdev, std::optional<std::shared_ptr<vfs::ops_t>> ops
                ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
                {
                    lib::unused(parent, name, rdev, ops);
                    if (stat::type(mode) == stat::s_ifreg)
                        return std::unexpected { lib::err::permission_denied };
                    return std::unexpected { lib::err::not_permitted };
                }

                auto symlink(
                    std::shared_ptr<vfs::inode_t> &parent,
                    std::string_view name, lib::path target
                ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
                {
                    lib::unused(parent, name, target);
                    return std::unexpected { lib::err::not_permitted };
                }

                auto link(
                    std::shared_ptr<vfs::inode_t> &parent,
                    std::string_view name, std::shared_ptr<vfs::inode_t> target
                ) -> lib::expect<std::shared_ptr<vfs::inode_t>> override
                {
                    lib::unused(parent, name, target);
                    return std::unexpected { lib::err::not_permitted };
                }

                auto unlink(std::shared_ptr<vfs::inode_t> &node) -> lib::expect<void> override
                {
                    lib::unused(node);
                    return std::unexpected { lib::err::not_permitted };
                }

                auto rename(
                    std::shared_ptr<vfs::inode_t> &old_parent, std::string_view old_name,
                    std::shared_ptr<vfs::inode_t> &new_parent, std::string_view new_name,
                    std::shared_ptr<vfs::inode_t> replaced
                ) -> lib::expect<void> override
                {
                    lib::unused(old_parent, old_name, new_parent, new_name, replaced);
                    return std::unexpected { lib::err::not_permitted };
                }

                auto readdir(std::shared_ptr<vfs::dentry_t> dir, std::size_t cookie)
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

                        result.push_back({
                            it->dentry->name,
                            it->dentry->inode,
                            it->cookie
                        });
                    }
                    return result;
                }

                auto lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
                    -> lib::expect<vfs::dir_entry> override
                {
                    const auto locked = dir->children.lock();
                    if (auto den = locked->lookup(name); den != nullptr)
                        return vfs::dir_entry { std::string { name }, den->inode, 0 };
                    return std::unexpected { lib::err::not_found };
                }

                auto write_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
                {
                    lib::unused(inode);
                    return { };
                }

                auto dirty_inode(std::shared_ptr<vfs::inode_t> &inode) -> lib::expect<void> override
                {
                    lib::unused(inode);
                    return { };
                }

                bool sync() override { return true; }

                bool unmount(std::shared_ptr<struct vfs::mount_t> mnt) override
                {
                    lib::unused(mnt);
                    return false;
                }

                std::shared_ptr<inode_t> mkdir(const std::shared_ptr<dev::kobject_t> &kobj)
                {
                    return std::make_shared<inode_t>(
                        inode_t::type::dir, kobj, nullptr, nullptr,
                        dev_id, next_inode++, ops_t::singleton()
                    );
                }

                std::shared_ptr<inode_t> mksym()
                {
                    return std::make_shared<inode_t>(
                        inode_t::type::symlink, nullptr, nullptr, nullptr,
                        dev_id, next_inode++, ops_t::singleton()
                    );
                }

                std::shared_ptr<inode_t> mkuevent(const std::shared_ptr<dev::kobject_t> &kobj)
                {
                    return std::make_shared<inode_t>(
                        inode_t::type::uevent, kobj, nullptr, nullptr,
                        dev_id, next_inode++, ops_t::singleton()
                    );
                }

                std::shared_ptr<inode_t> mkattr(
                    const std::shared_ptr<dev::kobject_t> &kobj, dev::attribute_t *attr
                )
                {
                    return std::make_shared<inode_t>(
                        inode_t::type::attr, kobj, attr, nullptr,
                        dev_id, next_inode++, ops_t::singleton()
                    );
                }

                std::shared_ptr<inode_t> mkbin(
                    const std::shared_ptr<dev::kobject_t> &kobj, dev::bin_attribute_t *battr
                )
                {
                    return std::make_shared<inode_t>(
                        inode_t::type::bin, kobj, nullptr, battr,
                        dev_id, next_inode++, ops_t::singleton()
                    );
                }

                std::shared_ptr<vfs::dentry_t> get_or_create(const std::shared_ptr<dev::kobject_t> &kobj)
                {
                    {
                        auto locked = nodes.lock();
                        if (auto it = locked->find(kobj.get()); it != locked->end())
                            return it->second;
                    }

                    std::shared_ptr<vfs::dentry_t> parent;
                    if (auto locked = kobj->parent.lock())
                        parent = get_or_create(locked);
                    else
                        parent = static_cast<struct fs_t *>(fs)->root;

                    auto locked = nodes.lock();
                    if (auto it = locked->find(kobj.get()); it != locked->end())
                        return it->second;

                    auto dentry = std::make_shared<vfs::dentry_t>();
                    dentry->name = kobj->name;
                    dentry->inode = mkdir(kobj);
                    dentry->parent = parent;

                    for (auto &attr : kobj->type.attributes())
                    {
                        auto child = std::make_shared<vfs::dentry_t>();
                        child->name = attr.name;
                        child->inode = mkattr(kobj, std::addressof(attr));
                        child->parent = dentry;
                        dentry->children.lock()->insert(std::move(child));
                    }

                    for (auto &battr : kobj->type.bin_attributes())
                    {
                        auto child = std::make_shared<vfs::dentry_t>();
                        child->name = battr.name;
                        child->inode = mkbin(kobj, std::addressof(battr));
                        child->parent = dentry;
                        dentry->children.lock()->insert(std::move(child));
                    }

                    if (kobj->as_device() || kobj->type != dev::empty_ktype())
                    {
                        auto child = std::make_shared<vfs::dentry_t>();
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
                    std::shared_ptr<vfs::dentry_t> dentry;
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
                    const std::shared_ptr<dev::kobject_t> &dir,
                    std::string_view name, const lib::path &target
                ) override
                {
                    auto dentry = get_or_create(dir);
                    auto locked = dentry->children.lock();
                    if (locked->lookup(name))
                        return;

                    auto child = std::make_shared<vfs::dentry_t>();
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
                    std::shared_ptr<vfs::dentry_t> dentry;
                    {
                        auto locked = nodes.lock();
                        const auto it = locked->find(dir.get());
                        if (it == locked->end())
                            return;

                        dentry = it->second;
                    }

                    dentry->children.lock()->erase(name);
                }

                ~instance_t() = default;
            };

            lib::locked_ptr<instance_t, sched::mutex> inst;
            std::shared_ptr<vfs::dentry_t> root;

            std::shared_ptr<struct vfs::mount_t> internal_mnt;
            mutable lib::locker<
                lib::list<
                    std::shared_ptr<struct vfs::mount_t>
                >, sched::mutex
            > mounts;

            auto mount(
                std::shared_ptr<vfs::dentry_t> src, std::uint64_t flags,
                std::optional<lib::maybe_uspan<const std::byte>> data
            ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
            {
                lib::unused(src, data, flags);

                auto mount = std::make_shared<struct vfs::mount_t>(inst, root);
                mounts.lock()->push_back(mount);
                return mount;
            }

            fs_t() : vfs::filesystem_t { "sysfs", 0x62656572 }
            {
                inst = lib::make_locked<instance_t, sched::mutex>();
                auto locked = inst.lock();
                locked->fs = this;

                root = std::make_shared<vfs::dentry_t>();
                root->name = "sysfs root";
                root->inode = locked->mkdir(nullptr);
                root->parent = root;

                internal_mnt = std::make_shared<struct vfs::mount_t>(inst, root);
            }
        };

        frg::manual_box<fs_t> fs;
    } // namespace

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.sysfs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.sysfs.mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.sysfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { dev::core_registered_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            fs.initialize();
            lib::bug_on(!vfs::register_fs(*fs));
            dev::attach_reflector(fs->inst.lock().get());
        }
    };

    lib::initgraph::task mount_task
    {
        "vfs.sysfs.mount",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            vfs::root_mounted_stage(),
            registered_stage()
        },
        lib::initgraph::entail { mounted_stage() },
        [] {
            const auto cerr = vfs::create(std::nullopt, "/sys", stat::s_ifdir | 0555);
            if (!cerr && cerr.error() != lib::err::already_exists)
            {
                lib::panic(
                    "sysfs: failed to create directory '/sys': {}",
                    lib::error_name(cerr.error())
                );
            }

            if (const auto merr = vfs::mount("", "/sys", "sysfs",
                vfs::ms_nosuid | vfs::ms_nodev | vfs::ms_noexec); !merr)
            {
                lib::panic(
                    "sysfs: failed to mount sysfs at '/sys': {}",
                    lib::error_name(merr.error())
                );
            }
        }
    };
} // namespace fs::sysfs
