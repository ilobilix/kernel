// Copyright (C) 2024-2026  ilobilo

module;

#include <version.h>

module drivers.fs.procfs;

import system.memory.virt;
import system.sched.mutex;
import system.sysctl;
import system.vfs.dev;
import system.vfs;
import fmt;

namespace fs::procfs
{
    namespace
    {
        struct entry
        {
            std::string name;
            gen_fn gen;
            mode_t mode;
            bool is_symlink;
        };

        lib::locker<std::vector<entry>, sched::mutex> global_registry;
        lib::locker<std::vector<entry>, sched::mutex> per_pid_registry;

        std::optional<entry> find_in(
            lib::locker<std::vector<entry>, sched::mutex> &registry,
            std::string_view name
        )
        {
            auto locked = registry.lock();
            for (const auto &it : *locked)
            {
                if (it.name.compare(name) == 0)
                    return it;
            }
            return std::nullopt;
        }

        bool register_in(
            lib::locker<std::vector<entry>, sched::mutex> &registry,
            std::string name, gen_fn gen, mode_t mode, bool is_symlink
        )
        {
            auto locked = registry.lock();
            for (const auto &it : *locked)
            {
                if (it.name.compare(name) == 0)
                    return false;
            }
            locked->push_back({ std::move(name), gen, mode, is_symlink });
            return true;
        }

        constexpr std::size_t max_readdir_batch = 256;
        constexpr std::size_t cookie_base = 3;

        std::optional<pid_t> parse_pid(std::string_view name)
        {
            if (name.empty())
                return std::nullopt;

            pid_t value = 0;
            for (char chr : name)
            {
                if (chr < '0' || chr > '9')
                    return std::nullopt;
                value = value * 10 + (chr - '0');
            }
            return value;
        }
    } // namespace

    bool register_global(std::string name, gen_fn gen, mode_t mode, bool is_symlink)
    {
        return register_in(global_registry, std::move(name), gen, mode, is_symlink);
    }

    bool register_per_pid(std::string name, gen_fn gen, mode_t mode, bool is_symlink)
    {
        return register_in(per_pid_registry, std::move(name), gen, mode, is_symlink);
    }

    enum class kind : std::uint8_t
    {
        root_dir,
        pid_dir,
        file,
        symlink,
        fd_dir,
        fd_link,
        sysctl_dir,
        sysctl_file
    };

    struct inode : vfs::inode
    {
        kind knd;
        pid_t pid;
        int fd;
        gen_fn gen;
        std::string sysctl_path;

        inode(kind knd, pid_t pid, int fd, gen_fn gen, dev_t dev, ino_t ino, mode_t mode)
            : vfs::inode { }, knd { knd }, pid { pid }, fd { fd }, gen { gen }
        {
            const bool is_link = (knd == kind::symlink || knd == kind::fd_link);
            const bool is_file = (knd == kind::file || knd == kind::sysctl_file);

            stat.st_dev = dev;
            stat.st_rdev = 0;
            stat.st_ino = ino;
            stat.st_mode = mode;
            stat.st_nlink = (is_file || is_link) ? 1 : 2;
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

    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        static bool is_readable(const inode *inod)
        {
            return (inod->knd == kind::file && inod->gen) || inod->knd == kind::sysctl_file;
        }

        static auto cache(std::shared_ptr<vfs::file> &file, inode *inod)
            -> lib::expect<std::shared_ptr<std::string>>
        {
            auto contents = std::static_pointer_cast<std::string>(file->private_data);
            if (contents)
                return contents;

            if (inod->knd == kind::sysctl_file)
            {
                const auto entry = sysctl::find(inod->sysctl_path);
                if (!entry || !entry->read)
                    return std::unexpected { lib::err::not_found };
                contents = std::make_shared<std::string>(entry->read());
            }
            else
            {
                sched::process_t *proc = nullptr;
                if (inod->pid >= 0)
                {
                    proc = sched::get_process(inod->pid);
                    if (!proc)
                        return std::unexpected { lib::err::not_found };
                }
                contents = std::make_shared<std::string>(inod->gen(proc));
            }
            file->private_data = contents;
            return contents;
        }

        lib::expect<void> open(std::shared_ptr<vfs::file> file, int flags, pid_t pid) override
        {
            lib::unused(flags, pid);
            auto inod = static_cast<inode *>(file->path.dentry->inode.get());
            if (!is_readable(inod))
                return { };

            if (const auto ret = cache(file, inod); !ret)
                return std::unexpected { ret.error() };
            return { };
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override
        {
            auto inod = static_cast<inode *>(file->path.dentry->inode.get());
            if (!is_readable(inod))
                return std::unexpected { lib::err::not_supported };

            auto cached = cache(file, inod);
            if (!cached)
                return std::unexpected { cached.error() };
            const auto &contents = *cached;

            if (offset >= contents->size())
                return 0uz;

            const auto remaining = contents->size() - offset;
            const auto to_copy = std::min<std::size_t>(buffer.size(), remaining);
            if (to_copy == 0)
                return 0uz;

            const auto sub = buffer.subspan(0, to_copy);
            if (!sub.copy_from(reinterpret_cast<const std::byte *>(contents->data() + offset)))
                return std::unexpected { lib::err::invalid_address };

            return to_copy;
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(offset);
            auto inod = static_cast<inode *>(file->path.dentry->inode.get());
            if (inod->knd != kind::sysctl_file)
                return std::unexpected { lib::err::read_only_fs };

            const auto entry = sysctl::find(inod->sysctl_path);
            if (!entry)
                return std::unexpected { lib::err::not_found };
            if (!entry->write)
                return std::unexpected { lib::err::read_only_fs };

            std::string data(buffer.size(), '\0');
            if (!buffer.copy_to(reinterpret_cast<std::byte *>(data.data())))
                return std::unexpected { lib::err::invalid_address };

            const auto ret = entry->write(data);
            if (!ret)
                return std::unexpected { ret.error() };

            file->private_data.reset();
            return buffer.size();
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(size);
            auto inod = static_cast<inode *>(file->path.dentry->inode.get());
            if (inod->knd == kind::sysctl_file)
            {
                file->private_data.reset();
                return { };
            }
            return std::unexpected { lib::err::read_only_fs };
        }

        lib::expect<vmm::object::ptr> map(std::shared_ptr<vfs::file> file) override
        {
            lib::unused(file);
            return std::unexpected { lib::err::mapping_unsupported };
        }
    };

    struct fs : vfs::filesystem
    {
        struct instance : vfs::filesystem::instance, std::enable_shared_from_this<instance>
        {
            static void apply_owner(inode &inod, pid_t pid)
            {
                if (pid < 0)
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

            std::shared_ptr<inode> make_root_inode()
            {
                return std::make_shared<inode>(
                    kind::root_dir, -1, -1, nullptr,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_ifdir) | 0555
                );
            }

            std::shared_ptr<inode> make_pid_dir_inode(pid_t pid)
            {
                auto ret = std::make_shared<inode>(
                    kind::pid_dir, pid, -1, nullptr,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_ifdir) | 0555
                );
                apply_owner(*ret, pid);
                return ret;
            }

            std::shared_ptr<inode> make_file_inode(pid_t pid, gen_fn gen, mode_t perm)
            {
                auto ret = std::make_shared<inode>(
                    kind::file, pid, -1, gen,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_ifreg) | perm
                );
                apply_owner(*ret, pid);
                return ret;
            }

            std::shared_ptr<inode> make_symlink_inode(pid_t pid, gen_fn gen, mode_t perm)
            {
                auto ret = std::make_shared<inode>(
                    kind::symlink, pid, -1, gen,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_iflnk) | perm
                );
                apply_owner(*ret, pid);
                return ret;
            }

            std::shared_ptr<inode> make_fd_dir_inode(pid_t pid)
            {
                auto ret = std::make_shared<inode>(
                    kind::fd_dir, pid, -1, nullptr,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_ifdir) | 0500
                );
                apply_owner(*ret, pid);
                return ret;
            }

            std::shared_ptr<inode> make_fd_link_inode(pid_t pid, int fd)
            {
                auto ret = std::make_shared<inode>(
                    kind::fd_link, pid, fd, nullptr,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_iflnk) | 0777
                );
                apply_owner(*ret, pid);
                return ret;
            }

            std::shared_ptr<inode> make_sysctl_dir_inode(std::string path)
            {
                auto ret = std::make_shared<inode>(
                    kind::sysctl_dir, -1, -1, nullptr,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_ifdir) | 0555
                );
                ret->sysctl_path = std::move(path);
                return ret;
            }

            std::shared_ptr<inode> make_sysctl_file_inode(std::string path, mode_t mode)
            {
                auto ret = std::make_shared<inode>(
                    kind::sysctl_file, -1, -1, nullptr,
                    dev_id, next_inode++,
                    static_cast<mode_t>(stat::s_ifreg) | mode
                );
                ret->sysctl_path = std::move(path);
                return ret;
            }

            auto make_entry_inode(pid_t pid, const entry &ent) -> std::shared_ptr<inode>
            {
                if (ent.is_symlink)
                    return make_symlink_inode(pid, ent.gen, ent.mode);
                return make_file_inode(pid, ent.gen, ent.mode);
            }

            auto create(
                std::shared_ptr<vfs::inode> &parent,
                std::string_view name, mode_t mode, dev_t rdev
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name, mode, rdev);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto symlink(
                std::shared_ptr<vfs::inode> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto link(
                std::shared_ptr<vfs::inode> &parent,
                std::string_view name, std::shared_ptr<vfs::inode> target
            ) -> lib::expect<std::shared_ptr<vfs::inode>> override
            {
                lib::unused(parent, name, target);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto unlink(std::shared_ptr<vfs::inode> &node) -> lib::expect<void> override
            {
                lib::unused(node);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto rename(
                std::shared_ptr<vfs::inode> &old_parent, std::string_view old_name,
                std::shared_ptr<vfs::inode> &new_parent, std::string_view new_name,
                std::shared_ptr<vfs::inode> replaced
            ) -> lib::expect<void> override
            {
                lib::unused(old_parent, old_name, new_parent, new_name, replaced);
                return std::unexpected { lib::err::read_only_fs };
            }

            auto readdir(std::shared_ptr<vfs::dentry> dir, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>> override
            {
                auto inod = static_cast<inode *>(dir->inode.get());
                if (inod->knd == kind::root_dir)
                    return readdir_root(cookie);
                if (inod->knd == kind::pid_dir)
                    return readdir_pid_dir(inod->pid, cookie);
                if (inod->knd == kind::fd_dir)
                    return readdir_fd_dir(inod->pid, cookie);
                if (inod->knd == kind::sysctl_dir)
                    return readdir_sysctl_dir(inod->sysctl_path, cookie);
                return std::unexpected { lib::err::not_a_dir };
            }

            auto lookup(std::shared_ptr<vfs::dentry> dir, std::string_view name)
                -> lib::expect<std::optional<vfs::dir_entry>> override
            {
                auto inod = static_cast<inode *>(dir->inode.get());

                if (inod->knd == kind::root_dir)
                {
                    if (name == "sys")
                    {
                        return vfs::dir_entry {
                            std::string { name },
                            make_sysctl_dir_inode({ }), 0
                        };
                    }
                    if (auto ent = find_in(global_registry, name))
                    {
                        return vfs::dir_entry {
                            std::string { name },
                            make_entry_inode(-1, std::move(*ent)), 0
                        };
                    }
                    if (auto pid = parse_pid(name))
                    {
                        if (!sched::get_process(*pid))
                            return std::nullopt;
                        return vfs::dir_entry {
                            std::string { name },
                            make_pid_dir_inode(*pid), 0
                        };
                    }
                    return std::nullopt;
                }

                if (inod->knd == kind::sysctl_dir)
                {
                    std::string child = inod->sysctl_path;
                    if (!child.empty())
                        child += '/';
                    child.append(name);

                    if (const auto entry = sysctl::find(child))
                    {
                        return vfs::dir_entry {
                            std::string { name },
                            make_sysctl_file_inode(std::move(child), entry->mode), 0
                        };
                    }
                    if (sysctl::dir_exists(child))
                    {
                        return vfs::dir_entry {
                            std::string { name },
                            make_sysctl_dir_inode(std::move(child)), 0
                        };
                    }
                    return std::nullopt;
                }

                if (inod->knd == kind::pid_dir)
                {
                    if (!sched::get_process(inod->pid))
                        return std::nullopt;

                    if (name == "fd")
                    {
                        return vfs::dir_entry {
                            std::string { name },
                            make_fd_dir_inode(inod->pid), 0
                        };
                    }
                    if (auto ent = find_in(per_pid_registry, name))
                    {
                        return vfs::dir_entry {
                            std::string { name },
                            make_entry_inode(inod->pid, std::move(*ent)), 0
                        };
                    }
                    return std::nullopt;
                }

                if (inod->knd == kind::fd_dir)
                {
                    auto proc = sched::get_process(inod->pid);
                    if (!proc)
                        return std::nullopt;

                    auto fd = parse_pid(name);
                    if (!fd)
                        return std::nullopt;

                    const std::unique_lock _ { proc->lock };
                    if (!proc->fdt || !proc->fdt->get(*fd))
                        return std::nullopt;

                    auto child = make_fd_link_inode(inod->pid, *fd);
                    return vfs::dir_entry {
                        std::string { name },
                        child, 0
                    };
                }

                return std::unexpected { lib::err::not_a_dir };
            }

            auto readlink(std::shared_ptr<vfs::dentry> dentry)
                -> lib::expect<lib::path> override
            {
                auto inod = static_cast<inode *>(dentry->inode.get());
                if (inod->knd == kind::fd_link)
                    return readlink_fd(inod);

                if (inod->knd != kind::symlink || !inod->gen)
                    return std::unexpected { lib::err::invalid_symlink };

                sched::process_t *proc = nullptr;
                if (inod->pid >= 0)
                {
                    proc = sched::get_process(inod->pid);
                    if (!proc)
                        return std::unexpected { lib::err::not_found };
                }
                auto target = inod->gen(proc);
                if (target.empty())
                    return std::unexpected { lib::err::not_found };
                return target;
            }

            bool revalidate(std::shared_ptr<vfs::dentry> dentry) override
            {
                auto inod = static_cast<inode *>(dentry->inode.get());
                if (inod->pid < 0)
                    return true;

                auto proc = sched::get_process(inod->pid);
                if (!proc)
                    return false;

                if (inod->knd == kind::fd_link)
                {
                    const std::unique_lock _ { proc->lock };
                    if (!proc->fdt || !proc->fdt->get(inod->fd))
                        return false;
                }
                return true;
            }

            bool permission(
                std::shared_ptr<vfs::dentry> dentry,
                const std::shared_ptr<sched::cred_t> &cred,
                std::uint32_t mode
            ) override
            {
                auto inod = static_cast<inode *>(dentry->inode.get());
                auto stat = inod->stat;
                if (inod->pid >= 0)
                {
                    if (auto proc = sched::get_process(inod->pid))
                    {
                        if (auto pcred = proc->cred)
                        {
                            stat.st_uid = pcred->ruid;
                            stat.st_gid = pcred->rgid;
                        }
                    }
                }
                return sched::check_perms(cred, stat, static_cast<sched::access_mode>(mode));
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

            ~instance() = default;

            private:
            static std::vector<entry> snapshot(lib::locker<std::vector<entry>, sched::mutex> &registry)
            {
                return *registry.lock();
            }

            auto readdir_root(std::size_t cookie) -> lib::expect<lib::list<vfs::dir_entry>>
            {
                lib::list<vfs::dir_entry> result;

                std::size_t idx = std::max<std::size_t>(cookie, cookie_base);
                if (idx == cookie_base)
                {
                    result.push_back({
                        std::string { "sys" },
                        make_sysctl_dir_inode({ }),
                        idx
                    });
                    idx++;
                }

                const auto entries = snapshot(global_registry);
                const std::size_t cookie_first_pid = cookie_base + 1 + entries.size();

                while (idx < cookie_first_pid && result.size() < max_readdir_batch)
                {
                    const auto &ent = entries[idx - cookie_base - 1];
                    auto child = make_entry_inode(-1, ent);
                    result.push_back({ ent.name, child, idx });
                    idx++;
                }

                if (result.size() >= max_readdir_batch)
                    return result;

                std::vector<pid_t> pids;
                sched::for_each_process([&](sched::process_t *proc) {
                    if (proc->pid > 0)
                        pids.push_back(proc->pid);
                    return true;
                });
                std::sort(pids.begin(), pids.end());

                std::size_t pid_idx = 0;
                if (idx > cookie_first_pid)
                    pid_idx = idx - cookie_first_pid;

                while (pid_idx < pids.size() && result.size() < max_readdir_batch)
                {
                    const auto pid = pids[pid_idx];
                    auto child = make_pid_dir_inode(pid);
                    result.push_back({
                        fmt::format("{}", pid),
                        child,
                        cookie_first_pid + pid_idx
                    });
                    pid_idx++;
                }

                return result;
            }

            auto readdir_pid_dir(pid_t pid, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>>
            {
                lib::list<vfs::dir_entry> result;
                if (!sched::get_process(pid))
                    return result;

                const auto entries = snapshot(per_pid_registry);
                std::size_t idx = std::max<std::size_t>(cookie, cookie_base);
                while (idx - cookie_base < entries.size())
                {
                    const auto &ent = entries[idx - cookie_base];
                    auto child = make_entry_inode(pid, ent);
                    result.push_back({ ent.name, child, idx });
                    idx++;
                }

                if (const auto fd_cookie = cookie_base + entries.size(); idx <= fd_cookie)
                {
                    result.push_back({
                        std::string { "fd" },
                        make_fd_dir_inode(pid),
                        fd_cookie
                    });
                }
                return result;
            }

            auto readdir_fd_dir(pid_t pid, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>>
            {
                lib::list<vfs::dir_entry> result;
                auto proc = sched::get_process(pid);
                if (!proc)
                    return result;

                std::vector<int> fds;
                {
                    const std::unique_lock _ { proc->lock };
                    if (!proc->fdt)
                        return result;
                    auto locked = proc->fdt->fds.read_lock();
                    fds.reserve(locked->size());
                    for (const auto &[fd, _ignored] : *locked)
                        fds.push_back(fd);
                }
                std::sort(fds.begin(), fds.end());

                const std::size_t base = cookie_base;
                std::size_t idx = std::max<std::size_t>(cookie, base);
                std::size_t fd_idx = idx - base;

                while (fd_idx < fds.size() && result.size() < max_readdir_batch)
                {
                    const int fd = fds[fd_idx];
                    auto child = make_fd_link_inode(pid, fd);
                    result.push_back({ fmt::format("{}", fd), child, base + fd_idx });
                    fd_idx++;
                }
                return result;
            }

            auto readdir_sysctl_dir(std::string_view path, std::size_t cookie)
                -> lib::expect<lib::list<vfs::dir_entry>>
            {
                lib::list<vfs::dir_entry> result;
                auto entries = sysctl::list(path);

                std::size_t entry_idx = cookie > cookie_base ? cookie - cookie_base : 0;
                while (entry_idx < entries.size() && result.size() < max_readdir_batch)
                {
                    auto &ent = entries[entry_idx];
                    std::string full = std::string { path };
                    if (!full.empty())
                        full += '/';
                    full.append(ent.name);

                    std::shared_ptr<inode> child;
                    if (ent.is_dir)
                        child = make_sysctl_dir_inode(std::move(full));
                    else if (const auto sct = sysctl::find(full))
                        child = make_sysctl_file_inode(std::move(full), sct->mode);

                    if (child)
                    {
                        result.push_back({
                            std::move(ent.name),
                            std::move(child),
                            cookie_base + entry_idx
                        });
                    }
                    entry_idx++;
                }
                return result;
            }

            auto readlink_fd(inode *inod) -> lib::expect<lib::path>
            {
                auto proc = sched::get_process(inod->pid);
                if (!proc)
                    return std::unexpected { lib::err::not_found };

                std::shared_ptr<vfs::filedesc> fdesc;
                {
                    const std::unique_lock _ { proc->lock };
                    if (!proc->fdt)
                        return std::unexpected { lib::err::not_found };
                    fdesc = proc->fdt->get(inod->fd);
                }
                if (!fdesc || !fdesc->file || !fdesc->file->path.dentry)
                    return std::unexpected { lib::err::not_found };

                const auto &target_inode = fdesc->file->path.dentry->inode;
                const auto target_ino = target_inode ? target_inode->stat.st_ino : 0;
                const auto type = target_inode ? target_inode->stat.type() : stat::s_ifreg;

                switch (type)
                {
                    case stat::s_ififo:
                        return fmt::format("pipe:[{}]", target_ino);
                    case stat::s_ifsock:
                        return fmt::format("socket:[{}]", target_ino);
                    default:
                        break;
                }

                auto target = vfs::pathname_from(fdesc->file->path);
                if (!target.empty())
                    return target;

                return fmt::format("anon_inode:[{}]", target_ino);
            }
        };

        lib::locked_ptr<instance, sched::mutex> inst;
        std::shared_ptr<vfs::dentry> root;

        std::shared_ptr<struct vfs::mount> internal_mnt;
        mutable lib::locker<
            lib::list<
                std::shared_ptr<struct vfs::mount>
            >, sched::mutex
        > mounts;

        auto mount(
            std::shared_ptr<vfs::dentry> src,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<struct vfs::mount>> override
        {
            lib::unused(src, data);

            auto mount = std::make_shared<struct vfs::mount>(inst, root, std::nullopt);
            mounts.lock()->push_back(mount);
            return mount;
        }

        fs() : vfs::filesystem { "proc", 0x9FA0 }
        {
            inst = lib::make_locked<instance, sched::mutex>();
            auto locked = inst.lock();
            locked->fs = this;

            root = std::make_shared<vfs::dentry>();
            root->name = "procfs root";
            root->inode = locked->make_root_inode();
            root->parent = root;

            internal_mnt = std::make_shared<struct vfs::mount>(inst, root, std::nullopt);

            vfs::dev::register_fs_ops(locked->dev_id, ops::singleton());
        }
    };

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
            register_global("version",
                [](auto) {
                    return fmt::format(
                        "Ilobilix version {} (built " __DATE__ " " __TIME__ ")\n",
                        ILOBILIX_RELEASE
                    );
                }, 0444
            );

            register_global("self",
                [](auto) {
                    return fmt::format("{}", sched::current_process()->pid);
                }, 0777, true
            );

            lib::bug_on(!vfs::register_fs(std::unique_ptr<vfs::filesystem> { new fs { } }));
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
