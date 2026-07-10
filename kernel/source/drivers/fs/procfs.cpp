// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

module drivers.fs.procfs;

import system.sched.mutex;
import system.vfs.dev;
import system.vfs;
import frigg;
import fmt;

namespace fs::procfs
{
    namespace
    {
        constexpr std::size_t max_readdir_batch = 256;
        constexpr std::size_t cookie_base = 3;
        constexpr mode_t def_dir_mode = 0555;

        enum class inode_type
        {
            root_dir,
            dir,
            file,
            symlink,
        };

        using registry_t = lib::locker<
            lib::map::flat_hash<
                std::string,
                node_t
            >, sched::mutex
        >;
        registry_t global_registry;

        struct dir_ops : node_ops
        {
            registry_t registry;
            lookup_fn lfn = nullptr;
            readdir_fn rfn = nullptr;

            lib::expect<node_t> lookup(sched::process_t *proc, std::string_view name) override
            {
                if (lfn)
                    return lfn(proc, name);

                auto locked = registry.lock();
                auto it = locked->find(name);
                if (it == locked->end())
                    return std::unexpected { lib::err::not_found };
                return it->second;
            }

            lib::expect<lib::list<node_t>> readdir(sched::process_t *proc) override
            {
                if (rfn)
                    return rfn(proc);

                lib::list<node_t> result;
                auto locked = registry.lock();
                for (const auto &[_, child] : *locked)
                    result.push_back(child);
                return result;
            }
        };

        bool register_in(
            registry_t &registry, lib::path path,
            std::shared_ptr<node_ops> ops, node_type type, mode_t mode
        )
        {
            auto current = std::addressof(registry);

            auto split = std::views::split(path.str(), '/');
            const std::size_t size = std::ranges::distance(split);
            if (size == 0)
                return false;

            for (std::size_t i = 0; const auto segment_view : split)
            {
                i++;
                const std::string_view segment { segment_view };
                if (segment.empty())
                    continue;
                lib::bug_on(segment == "..");

                const bool last = (i == size);
                auto locked = current->lock();

                auto it = locked->find(segment);
                if (!last)
                {
                    std::shared_ptr<dir_ops> next_dir;
                    if (it == locked->end())
                    {
                        next_dir = std::static_pointer_cast<dir_ops>(make_dir_ops());
                        (*locked)[segment] = node_t {
                            std::string { segment },
                            def_dir_mode, node_type::dir, next_dir
                        };
                    }
                    else
                    {
                        if (it->second.type != node_type::dir)
                            return false;

                        next_dir = std::static_pointer_cast<dir_ops>(it->second.ops);
                    }

                    current = std::addressof(next_dir->registry);
                    continue;
                }

                if (it != locked->end())
                    return false;

                (*locked)[segment] = node_t {
                    std::string { segment },
                    mode, type, ops
                };
                return true;
            }
            std::unreachable();
        }

        std::optional<node_t> find_in(registry_t &registry, std::string_view name)
        {
            const auto locked = registry.lock();
            auto it = locked->find(name);
            if (it == locked->end())
                return std::nullopt;
            return it->second;
        }

        struct file_ops : node_ops
        {
            gen_fn gfn = nullptr;
            stream_fn sfn = nullptr;
            write_fn wfn = nullptr;
            readlink_fn rdlfn = nullptr;
            revalidate_fn rvfn = nullptr;

            bool can_trunc() override { return wfn != nullptr; }
            bool can_stream() override { return sfn != nullptr; }

            lib::expect<std::string> generate(sched::process_t *proc) override
            {
                if (!gfn)
                    return node_ops::generate(proc);
                return gfn(proc);
            }

            lib::expect<std::size_t> read_at(sched::process_t *proc, std::uint64_t offset, std::span<char> out) override
            {
                if (!sfn)
                    return node_ops::read_at(proc, offset, out);
                return sfn(proc, offset, out);
            }

            lib::expect<void> write(sched::process_t *proc, std::string_view data) override
            {
                if (!wfn)
                    return node_ops::write(proc, data);
                return wfn(proc, data);
            }

            lib::expect<lib::path> readlink(sched::process_t *proc) override
            {
                if (!rdlfn)
                    return node_ops::readlink(proc);
                return rdlfn(proc);
            }

            bool revalidate(sched::process_t *proc) override
            {
                if (!rvfn)
                    return node_ops::revalidate(proc);
                return rvfn(proc);
            }
        };

        std::shared_ptr<dir_ops> pid_dir_ops()
        {
            static auto instance = std::shared_ptr<dir_ops>(new dir_ops { });
            return instance;
        }

        struct inode_t : vfs::inode_t
        {
            inode_type type;
            std::shared_ptr<node_ops> ops;

            pid_t pid;
            int fd;

            inode_t(
                inode_type type, std::shared_ptr<node_ops> ops, pid_t pid, int fd,
                dev_t dev, ino_t ino, mode_t mode, std::shared_ptr<vfs::ops_t> iops
            ) : vfs::inode_t { iops }, type { type },
                ops { ops }, pid { pid }, fd { fd }
            {
                stat.st_dev = dev;
                stat.st_rdev = 0;
                stat.st_ino = ino;
                stat.st_mode = mode;
                stat.st_nlink = 1;
                stat.st_uid = 0;
                stat.st_gid = 0;
                stat.st_size = 0;
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

                std::shared_ptr<sched::process_t> proc;
                if (inod->pid > 0)
                {
                    proc = sched::get_process(inod->pid);
                    if (!proc)
                        return std::unexpected { lib::err::not_found };
                }

                if (inod->ops->can_stream())
                {
                    if (buffer.size() == 0)
                        return 0uz;

                    lib::buffer<char> tmp { buffer.size() };
                    const auto produced = inod->ops->read_at(proc.get(), offset, tmp.span());
                    if (!produced)
                        return std::unexpected { produced.error() };
                    if (*produced == 0)
                        return 0uz;

                    if (!buffer.subspan(0, *produced)
                        .copy_from(reinterpret_cast<const std::byte *>(tmp.data())))
                        return std::unexpected { lib::err::invalid_address };
                    return *produced;
                }

                if (!file->private_data || offset == 0)
                {
                    auto content = inod->ops->generate(proc.get());
                    if (!content)
                        return std::unexpected { content.error() };
                    file->private_data = std::shared_ptr<std::string> {
                        new std::string { std::move(*content) }
                    };
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
                lib::unused(offset);

                const auto inod = std::static_pointer_cast<inode_t>(file->path.dentry->inode);

                std::shared_ptr<sched::process_t> proc;
                if (inod->pid > 0)
                {
                    proc = sched::get_process(inod->pid);
                    if (!proc)
                        return std::unexpected { lib::err::not_found };
                }

                std::string data(buffer.size(), '\0');
                if (!buffer.copy_to(reinterpret_cast<std::byte *>(data.data())))
                    return std::unexpected { lib::err::invalid_address };

                if (const auto ret = inod->ops->write(proc.get(), data); !ret)
                    return std::unexpected { ret.error() };

                file->private_data.reset();
                return buffer.size();
            }

            bool truncable() const override { return true; }
            lib::expect<void> trunc(std::shared_ptr<vfs::file_t> file, std::size_t size) override
            {
                lib::unused(size);
                file->private_data.reset();
                return { };
            }
        };

        struct fs_t : vfs::filesystem_t
        {
            struct instance_t : vfs::filesystem_t::instance_t,
                std::enable_shared_from_this<instance_t>
            {
                static void apply_owner(inode_t &inod, pid_t pid)
                {
                    if (pid <= 0)
                        return;

                    auto proc = sched::get_process(pid);
                    if (!proc)
                        return;

                    const auto cred = proc->cred;
                    if (!cred)
                        return;

                    inod.stat.st_uid = cred->ruid;
                    inod.stat.st_gid = cred->rgid;
                }

                std::shared_ptr<inode_t> mkroot()
                {
                    return std::make_shared<inode_t>(
                        inode_type::root_dir, nullptr, -1, -1,
                        dev_id, next_inode++,
                        static_cast<mode_t>(stat::s_ifdir) | 0555,
                        ops_t::singleton()
                    );
                }

                auto mkfile(pid_t pid, std::shared_ptr<node_ops> ops, mode_t mode)
                    -> std::shared_ptr<inode_t>
                {
                    auto ret = std::make_shared<inode_t>(
                        inode_type::file, std::move(ops), pid, -1,
                        dev_id, next_inode++,
                        static_cast<mode_t>(stat::s_ifreg) | mode,
                        ops_t::singleton()
                    );
                    apply_owner(*ret, pid);
                    return ret;
                }

                auto mksym(pid_t pid, std::shared_ptr<node_ops> ops, mode_t mode)
                    -> std::shared_ptr<inode_t>
                {
                    auto ret = std::make_shared<inode_t>(
                        inode_type::symlink, std::move(ops), pid, -1,
                        dev_id, next_inode++,
                        static_cast<mode_t>(stat::s_iflnk) | mode,
                        ops_t::singleton()
                    );
                    apply_owner(*ret, pid);
                    return ret;
                }

                auto mkdir(pid_t pid, std::shared_ptr<node_ops> ops, mode_t mode)
                    -> std::shared_ptr<inode_t>
                {
                    auto ret = std::make_shared<inode_t>(
                        inode_type::dir, std::move(ops), pid, -1,
                        dev_id, next_inode++,
                        static_cast<mode_t>(stat::s_ifdir) | mode,
                        ops_t::singleton()
                    );
                    apply_owner(*ret, pid);
                    return ret;
                }

                auto mkinode(pid_t pid, const node_t &node) -> std::shared_ptr<inode_t>
                {
                    switch (node.type)
                    {
                        case node_type::file:
                            return mkfile(pid, node.ops, node.mode);
                        case node_type::symlink:
                            return mksym(pid, node.ops, node.mode);
                        case node_type::dir:
                            return mkdir(pid, node.ops, node.mode);
                    }
                    std::unreachable();
                }

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
                    const auto inod = std::static_pointer_cast<inode_t>(dir->inode);
                    lib::list<vfs::dir_entry> result;

                    switch (inod->type)
                    {
                        case inode_type::root_dir:
                        {
                            std::size_t idx = cookie_base;
                            {
                                const auto locked = global_registry.lock();
                                for (const auto &[name, node] : *locked)
                                {
                                    if (idx++ <= cookie)
                                        continue;

                                    result.emplace_back(
                                        std::string { name },
                                        mkinode(-1, node), idx
                                    );

                                    if (result.size() >= max_readdir_batch)
                                        return result;
                                }
                            }

                            sched::for_each_process([&](const std::shared_ptr<sched::process_t> &proc) {
                                if (proc->pid == 0)
                                    return true;

                                if (idx++ <= cookie)
                                    return false;

                                result.emplace_back(
                                    std::to_string(proc->pid),
                                    mkdir(proc->pid, pid_dir_ops(), 0555), idx
                                );
                                return true;
                            });

                            return result;
                        }
                        case inode_type::dir:
                        {
                            if (!inod->ops)
                                return result;

                            std::shared_ptr<sched::process_t> proc;
                            if (inod->pid > 0)
                            {
                                proc = sched::get_process(inod->pid);
                                if (!proc)
                                    return std::unexpected { lib::err::not_found };
                            }

                            auto nodes = inod->ops->readdir(proc.get());
                            if (!nodes)
                                return std::unexpected { nodes.error() };

                            std::size_t idx = cookie_base;
                            for (const auto &node : *nodes)
                            {
                                if (idx++ < cookie)
                                    continue;

                                result.emplace_back(
                                    std::string { node.name },
                                    mkinode(inod->pid, node), idx
                                );

                                if (result.size() >= max_readdir_batch)
                                    return result;
                            }

                            return result;
                        }
                        case inode_type::file:
                        case inode_type::symlink:
                            return std::unexpected { lib::err::not_a_dir };
                    }
                    std::unreachable();
                }

                auto lookup(std::shared_ptr<vfs::dentry_t> dir, std::string_view name)
                    -> lib::expect<vfs::dir_entry> override
                {
                    const auto inod = std::static_pointer_cast<inode_t>(dir->inode);
                    switch (inod->type)
                    {
                        case inode_type::root_dir:
                        {
                            if (const auto node = find_in(global_registry, name))
                            {
                                return vfs::dir_entry {
                                    std::string { name },
                                    mkinode(-1, *node), 0
                                };
                            }

                            char *end;
                            const auto res = lib::str2int<pid_t>(name.data(), &end, 10);
                            if (!res.has_value() || end != name.data() + name.size())
                                return std::unexpected { lib::err::not_found };
                            const auto pid = *res;

                            if (!sched::get_process(pid))
                                return std::unexpected { lib::err::not_found };

                            return vfs::dir_entry {
                                std::string { name },
                                mkdir(pid, pid_dir_ops(), 0555), 0
                            };
                        }
                        case inode_type::dir:
                        {
                            if (!inod->ops)
                                return std::unexpected { lib::err::not_found };

                            std::shared_ptr<sched::process_t> proc;
                            if (inod->pid > 0)
                            {
                                proc = sched::get_process(inod->pid);
                                if (!proc)
                                    return std::unexpected { lib::err::not_found };
                            }

                            auto result = inod->ops->lookup(proc.get(), name);
                            if (!result)
                                return std::unexpected { result.error() };

                            return vfs::dir_entry {
                                std::string { name },
                                mkinode(inod->pid, *result), 0
                            };
                        }
                        case inode_type::file:
                        case inode_type::symlink:
                            return std::unexpected { lib::err::not_a_dir };
                    }
                    std::unreachable();
                }

                auto readlink(std::shared_ptr<vfs::dentry_t> dentry)
                    -> lib::expect<lib::path> override
                {
                    const auto inod = std::static_pointer_cast<inode_t>(dentry->inode);
                    if (inod->type != inode_type::symlink)
                        return std::unexpected { lib::err::invalid_symlink };

                    std::shared_ptr<sched::process_t> proc;
                    if (inod->pid > 0)
                    {
                        proc = sched::get_process(inod->pid);
                        if (!proc)
                            return std::unexpected { lib::err::not_found };
                    }

                    return inod->ops->readlink(proc.get());
                }

                bool revalidate(std::shared_ptr<vfs::dentry_t> dentry) override
                {
                    const auto inod = std::static_pointer_cast<inode_t>(dentry->inode);
                    if (inod->pid <= 0)
                        return true;

                    auto proc = sched::get_process(inod->pid);
                    if (!proc)
                        return false;

                    return !(inod->ops && !inod->ops->revalidate(proc.get()));
                }

                bool permission(
                    std::shared_ptr<vfs::dentry_t> dentry,
                    const std::shared_ptr<sched::cred_t> &cred,
                    std::uint32_t mode
                ) override
                {
                    const auto inod = std::static_pointer_cast<inode_t>(dentry->inode);
                    auto stat = inod->stat;
                    if (inod->pid > 0)
                    {
                        if (auto proc = sched::get_process(inod->pid))
                        {
                            lib::bug_on(!proc->cred);
                            stat.st_uid = proc->cred->ruid;
                            stat.st_gid = proc->cred->rgid;
                        }
                    }
                    return sched::check_perms(cred, stat, static_cast<sched::access_mode>(mode));
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
                std::shared_ptr<vfs::dentry_t> src,
                std::optional<lib::maybe_uspan<const std::byte>> data
            ) const -> lib::expect<std::shared_ptr<struct vfs::mount_t>> override
            {
                lib::unused(src, data);

                auto mount = std::make_shared<struct vfs::mount_t>(inst, root);
                mounts.lock()->push_back(mount);
                return mount;
            }

            fs_t() : vfs::filesystem_t { "proc", 0x9FA0 }
            {
                inst = lib::make_locked<instance_t, sched::mutex>();
                auto locked = inst.lock();
                locked->fs = this;

                root = std::make_shared<vfs::dentry_t>();
                root->name = "procfs root";
                root->inode = locked->mkroot();
                root->parent = root;

                internal_mnt = std::make_shared<struct vfs::mount_t>(inst, root);
            }
        };

        frg::manual_box<fs_t> fs;
    } // namespace

    std::shared_ptr<node_ops> make_file_ops(gen_fn gfn, write_fn wfn)
    {
        auto ret = std::shared_ptr<file_ops>(new file_ops { });
        ret->gfn = gfn;
        ret->wfn = wfn;
        return ret;
    }

    std::shared_ptr<node_ops> make_streaming_file_ops(stream_fn sfn, write_fn wfn)
    {
        auto ret = std::shared_ptr<file_ops>(new file_ops { });
        ret->sfn = sfn;
        ret->wfn = wfn;
        return ret;
    }

    std::shared_ptr<node_ops> make_symlink_ops(readlink_fn rdlfn, revalidate_fn rvfn)
    {
        auto ret = std::shared_ptr<file_ops>(new file_ops { });
        ret->rdlfn = rdlfn;
        ret->rvfn = rvfn;
        return ret;
    }

    std::shared_ptr<node_ops> make_dir_ops(lookup_fn lfn, readdir_fn rfn)
    {
        auto ret = std::shared_ptr<dir_ops>(new dir_ops { });
        ret->lfn = lfn;
        ret->rfn = rfn;
        return ret;
    }

    bool register_global(
        lib::path path, std::shared_ptr<node_ops> ops,
        node_type type, mode_t mode
    )
    {
        lib::bug_on(path.empty() || !ops);
        return register_in(global_registry, path, ops, type, mode);
    }

    bool register_per_pid(
        lib::path path, std::shared_ptr<node_ops> ops,
        node_type type, mode_t mode
    )
    {
        lib::bug_on(path.empty() || !ops);
        return register_in(pid_dir_ops()->registry, path, ops, type, mode);
    }

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.procfs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.procfs.mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task register_task
    {
        "vfs.procfs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::entail { registered_stage() },
        [] {
            fs.initialize();

            lib::bug_on(!register_global("version",
                make_file_ops([](auto) {
                    return fmt::format(
                        "Ilobilix version {} (built " __DATE__ " " __TIME__ ")\n",
                        ILOBILIX_RELEASE
                    );
                }), node_type::file, 0444
            ));

            lib::bug_on(!register_global("self",
                make_symlink_ops([](auto) {
                    return fmt::format("{}", sched::current_process()->pid);
                }), node_type::symlink, 0777
            ));

            // TODO: stub
            lib::bug_on(!register_per_pid("cgroup",
                make_file_ops([](auto) {
                    return "0::/\n";
                }), node_type::file, 0444
            ));

            lib::bug_on(!vfs::register_fs(*fs));
        }
    };

    lib::initgraph::task mount_task
    {
        "vfs.procfs.mount",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            vfs::root_mounted_stage(),
            registered_stage()
        },
        lib::initgraph::entail { mounted_stage() },
        [] {
            const auto cerr = vfs::create(std::nullopt, "/proc", stat::s_ifdir | 0555);
            if (!cerr && cerr.error() != lib::err::already_exists)
            {
                lib::panic(
                    "procfs: failed to create directory '/proc': {}",
                    lib::error_name(cerr.error())
                );
            }

            if (const auto merr = vfs::mount("", "/proc", "proc",
                vfs::ms_nosuid | vfs::ms_nodev | vfs::ms_noexec); !merr)
            {
                lib::panic(
                    "procfs: failed to mount procfs at '/proc': {}",
                    lib::error_name(merr.error())
                );
            }
        }
    };
} // namespace fs::procfs
