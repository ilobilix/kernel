// Copyright (C) 2024-2026  ilobilo

module system.vfs;

import system.cpu.local;
import system.vfs.dev;
import system.bin.elf;
import system.sched;
import drivers.fs;
import drivers.fs.procfs;
import fmt;

namespace vfs
{
    namespace
    {
        std::shared_ptr<dentry_t> root = [] {
            auto root = dentry_t::create();
            root->name = "/";
            root->parent = root;
            root->inode = std::make_shared<inode_t>(nullptr);
            root->inode->stat.st_mode = static_cast<mode_t>(stat::type::s_ifdir) |
                (s_irwxu | s_irgrp | s_ixgrp | s_iroth | s_ixoth);
            return root;
        } ();

        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                filesystem_t *
            >, sched::mutex
        > filesystems;

        lib::locker<
            lib::map::flat_hash<
                std::size_t,
                std::shared_ptr<struct mount_t>
            >, sched::mutex
        > mounts;

        std::atomic<dev_t> next_dev = 1;
        dev_t allocate_dev()
        {
            return next_dev.fetch_add(1, std::memory_order_relaxed);
        }

        std::atomic<ino_t> next_mount_id { 1 };
        std::size_t allocate_mount_id()
        {
            return next_mount_id.fetch_add(1, std::memory_order_relaxed);
        }

        std::atomic<ino_t> next_anon_ino { 1 };
        ino_t allocate_anon_ino()
        {
            return next_anon_ino.fetch_add(1, std::memory_order_relaxed);
        }

        path_t resolve_mounts(path_t path)
        {
            while (path.mnt != nullptr && path.dentry == path.mnt->root)
            {
                if (!path.mnt->mounted_on.has_value())
                    break;
                path = path.mnt->mounted_on.value();
            }
            return path;
        };

        auto resolve_real_dir(std::optional<path_t> anchor, lib::path dir) -> lib::expect<path_t>
        {
            auto res = resolve(anchor, dir);
            if (!res)
                return std::unexpected { res.error() };
            if (res->target.dentry->inode->stat.type() != stat::type::s_iflnk)
                return std::move(res->target);

            auto reduced = reduce(res->parent, res->target);
            if (!reduced)
                return std::unexpected { reduced.error() };
            return std::move(*reduced);
        }

        bool is_unsupported_xattr(std::string_view name)
        {
            return name.starts_with("system.");
        }

        bool can_read_xattr(const std::shared_ptr<sched::cred_t> &cred, std::string_view name)
        {
            if (name.starts_with("security.") || name.starts_with("trusted."))
                return sched::capable(cred, sched::cap_t::sys_admin);
            return true;
        }

        bool can_write_xattr(
            const std::shared_ptr<sched::cred_t> &cred,
            const stat &stat, std::string_view name
        )
        {
            if (name == xattr_caps_name)
                return sched::capable(cred, sched::cap_t::setfcap);
            if (name.starts_with("security.") || name.starts_with("trusted."))
                return sched::capable(cred, sched::cap_t::sys_admin);
            if (name.starts_with("user."))
                return sched::check_perms(cred, stat, sched::access_mode::write);
            return false;
        }

        std::string get_mnt_opts(const auto flags, const auto &fs)
        {
            std::string opts { (flags & ms_rdonly) ? "ro" : "rw" };
            if (flags & ms_nosuid)
                opts.append(",nosuid");
            if (flags & ms_nodev)
                opts.append(",nodev");
            if (flags & ms_noexec)
                opts.append(",noexec");
            if (flags & ms_synchronous)
                opts.append(",sync");
            if (flags & ms_dirsync)
                opts.append(",dirsync");
            if (flags & ms_noatime)
                opts.append(",noatime");
            if (flags & ms_nodiratime)
                opts.append(",nodiratime");
            if (flags & ms_relatime)
                opts.append(",relatime");
            if (flags & ms_strictatime)
                opts.append(",strictatime");
            if (flags & ms_lazytime)
                opts.append(",lazytime");

            const auto locked = fs.lock();
            const auto fs_opts = locked->mount_options();
            if (!fs_opts.empty())
            {
                opts.append(",");
                opts.append(fs_opts);
            }

            return opts;
        };
    } // namespace

    filesystem_t::instance_t::instance_t() : dev_id { allocate_dev() } { }

    auto filesystem_t::instance_t::readlink(std::shared_ptr<dentry_t> dentry) -> lib::expect<lib::path>
    {
        if (!dentry || dentry->symlinked_to.empty())
            return std::unexpected { lib::err::invalid_symlink };
        return dentry->symlinked_to;
    }

    bool filesystem_t::instance_t::permission(
        std::shared_ptr<dentry_t> dentry,
        const std::shared_ptr<sched::cred_t> &cred,
        std::uint32_t mode
    )
    {
        return sched::check_perms(cred, dentry->inode->stat, static_cast<sched::access_mode>(mode));
    }

    void filesystem_t::instance_t::statfs(struct ::statfs &out)
    {
        out.f_type = fs ? static_cast<std::int64_t>(fs->magic) : 0;
        out.f_bsize = 4096;
        out.f_namelen = 255;
        out.f_frsize = 4096;
        out.f_fsid.val[0] = static_cast<std::int32_t>(dev_id);
        out.f_fsid.val[1] = static_cast<std::int32_t>(dev_id >> 32);
    }

    bool check_access(
        const path_t &target,
        const std::shared_ptr<sched::cred_t> &cred,
        std::uint32_t mode
    )
    {
        if (!target.dentry || !target.dentry->inode)
            return false;

        if (target.mnt)
        {
            auto fs = target.mnt->fs.lock();
            return fs->permission(target.dentry, cred, mode);
        }
        return sched::check_perms(cred, target.dentry->inode->stat, static_cast<sched::access_mode>(mode));
    }

    bool check_access(const path_t &target, std::uint32_t mode)
    {
        return check_access(target, sched::current_process()->cred, mode);
    }

    std::shared_ptr<ops_t> inode_t::get_ops()
    {
        if (ops)
            return ops;
        return dev::get_ops(stat.st_rdev, stat.st_mode).value_or(nullptr);
    }

    lib::expect<std::size_t> file_t::getdents(lib::maybe_uspan<std::byte> buffer)
    {
        if (!ops)
            return std::unexpected { lib::err::invalid_device_or_address };

        const std::unique_lock _ { lock };
        std::size_t progress = 0;
        if (offset < 3)
        {
            if (offset < 2)
            {
                static const auto dotstr = std::as_bytes(std::span { "." });
                static const std::size_t reclen = (sizeof(vfs::dirent64) + dotstr.size() + 7) & ~7;

                if (progress + reclen > buffer.size())
                    return std::unexpected { lib::err::buffer_too_small };

                const auto ino = path.dentry->inode->stat.st_ino;
                vfs::dirent64 dot
                {
                    .d_ino = ino,
                    .d_off = 2,
                    .d_reclen = static_cast<std::uint16_t>(reclen),
                    .d_type = vfs::stat_to_dt(stat::type::s_ifdir)
                };

                buffer.subspan(progress, sizeof(vfs::dirent64))
                    .copy_from(reinterpret_cast<std::byte *>(&dot));
                buffer.subspan(progress + sizeof(vfs::dirent64), 2)
                    .copy_from(dotstr);

                progress += reclen;
                offset = 2;
            }

            if (offset == 2)
            {
                static const auto dotdotstr = std::as_bytes(std::span { ".." });
                static const std::size_t reclen = (sizeof(vfs::dirent64) + dotdotstr.size() + 7) & ~7;

                if (progress + reclen > buffer.size())
                {
                    if (progress == 0)
                        return std::unexpected { lib::err::buffer_too_small };
                    return progress;
                }

                const auto parent_ino = resolve_mounts(path).dentry->parent.lock()->inode->stat.st_ino;

                vfs::dirent64 dotdot
                {
                    .d_ino = parent_ino,
                    .d_off = 3,
                    .d_reclen = static_cast<std::uint16_t>(reclen),
                    .d_type = vfs::stat_to_dt(stat::type::s_ifdir)
                };

                buffer.subspan(progress, sizeof(vfs::dirent64))
                    .copy_from(reinterpret_cast<std::byte *>(&dotdot));
                buffer.subspan(progress + sizeof(vfs::dirent64), 3)
                    .copy_from(dotdotstr);

                progress += reclen;
                offset++;
            }
        }

        auto fs = path.mnt->fs.lock();
        while (true)
        {
            auto batch = fs->readdir(path.dentry, offset);
            if (!batch)
                return std::unexpected { batch.error() };

            if (batch->empty())
                break;

            for (const auto &entry : *batch)
            {
                const auto reclen = (sizeof(vfs::dirent64) + entry.name.size() + 1 + 7) & ~7;

                if (progress + reclen > buffer.size())
                {
                    if (progress == 0)
                        return std::unexpected { lib::err::buffer_too_small };
                    return progress;
                }

                vfs::dirent64 dirent
                {
                    .d_ino = entry.inode->stat.st_ino,
                    .d_off = static_cast<std::int64_t>(entry.cookie + 1),
                    .d_reclen = static_cast<std::uint16_t>(reclen),
                    .d_type = vfs::stat_to_dt(entry.inode->stat.type())
                };

                buffer.subspan(progress, sizeof(dirent))
                    .copy_from(reinterpret_cast<std::byte *>(&dirent));

                buffer.subspan(progress + sizeof(dirent), entry.name.size() + 1)
                    .copy_from(reinterpret_cast<const std::byte *>(entry.name.c_str()));

                progress += reclen;
                offset = entry.cookie + 1;
            }
        }

        return progress;
    }

    auto filesystem_t::instance_t::lookup(std::shared_ptr<dentry_t> dir, std::string_view name)
        -> lib::expect<dir_entry>
    {
        if (auto den = dir->children.lock()->lookup(name))
            return dir_entry { std::string { name }, den->inode, 0 };

        std::size_t cookie = 3;
        while (true)
        {
            auto batch = readdir(dir, cookie);
            if (!batch)
                return std::unexpected { batch.error() };

            if (batch->empty())
                return std::unexpected { lib::err::not_found };

            auto locked = dir->children.lock();
            for (auto &entry : *batch)
            {
                if (!locked->lookup(entry.name))
                {
                    auto dentry = vfs::dentry_t::create();
                    dentry->parent = dir;
                    dentry->name = entry.name;
                    dentry->inode = entry.inode;
                    locked->insert(dentry);
                }

                if (entry.name == name)
                    return entry;

                cookie = entry.cookie + 1;
            }
        }
    }

    path_t get_root(bool absolute)
    {
        if (!absolute)
        {
            auto proc = sched::current_process();
            if (proc->vfs) [[likely]]
                return proc->vfs->root;
        }

        path_t ret { .mnt = nullptr, .dentry = dentry_t::root(true) };
        while (true)
        {
            std::shared_ptr<struct mount_t> mnt;
            {
                const auto locked = ret.dentry->child_mounts.lock();
                if (locked->empty())
                    break;
                mnt = locked->back().lock();
            }
            if (!mnt || !mnt->root)
                break;

            ret.mnt = mnt;
            ret.dentry = mnt->root;
        }
        return ret;
    }

    std::shared_ptr<struct mount_t> get_mount(std::size_t id)
    {
        const auto locked = mounts.lock();
        auto it = locked->find(id);
        if (it == locked->end())
            return nullptr;
        return it->second;
    }

    bool register_fs(filesystem_t &fs)
    {
        auto locked = filesystems.lock();
        if (locked->contains(fs.name))
            return false;

        lib::info("vfs: registering filesystem '{}'", fs.name);
        locked.value()[fs.name] = &fs;
        return true;
    }

    bool unregister_fs(filesystem_t &fs)
    {
        lib::unused(fs);
        lib::panic("TODO: unregister_fs");
        std::unreachable();
    }

    filesystem_t *find_fs(std::string_view name)
    {
        {
            const auto locked = filesystems.lock();
            if (auto it = locked->find(name); it != locked->end())
                return it->second;
        }

        if (!bin::elf::mod::request_alias("fs-" + std::string { name }))
            return nullptr;

        const auto locked = filesystems.lock();
        if (auto it = locked->find(name); it != locked->end())
            return it->second;
        return nullptr;
    }

    std::shared_ptr<dentry_t> dentry_t::root(bool absolute)
    {
        if (!absolute)
        {
            auto proc = sched::current_process();
            if (proc->vfs) [[likely]]
                return proc->vfs->root.dentry;
        }
        return vfs::root;
    }

    std::shared_ptr<dentry_t> dentry_t::create()
    {
        return std::make_shared<dentry_t>();
    }

    std::string pathname_from(path_t path)
    {
        std::size_t len = 0;
        std::vector<std::string_view> segments;

        while (true)
        {
            path = resolve_mounts(path);
            if (path.dentry == nullptr || path.dentry == vfs::root)
                break;

            auto parent = path.dentry->parent.lock();
            if (path.dentry == parent)
                break;

            segments.push_back(path.dentry->name);
            len += path.dentry->name.size();
            path.dentry = parent;
        }

        std::string result;
        result.reserve(len + segments.size());

        for (auto it = segments.rbegin(); it != segments.rend(); it++)
        {
            result += '/';
            result += *it;
        }

        if (result.empty())
            result = "/";
        return result;
    }

    auto path_for(lib::path _path) -> lib::expect<path_t>
    {
        // hmmm is this correct?
        auto res = resolve(std::nullopt, _path);
        if (!res)
            return std::unexpected { res.error() };
        return res->target;
    }

    auto resolve(std::optional<path_t> parent, lib::path _path, bool automount)
        -> lib::expect<resolve_res>
    {
        if (!parent || _path.is_absolute())
            parent = get_root(false);

        lib::bug_on(!parent.has_value());

        if (_path.str() == "/"sv || _path.empty() || _path.str() == "."sv)
            return resolve_res { parent.value(), parent.value() };

        lib::bug_on(parent->mnt == nullptr);

        auto current = parent.value();
        const auto check_search = [](const path_t &path) {
            return check_access(path, static_cast<std::uint32_t>(sched::access_mode::exec));
        };

        auto split = std::views::split(_path.str(), '/');
        const std::size_t size = std::ranges::distance(split);
        for (std::size_t i = 0; const auto segment_view : split)
        {
            i++;
            const std::string_view segment { segment_view };
            if (segment.empty())
                continue;

            const bool last = (i == size);

            if (segment == "..")
            {
                auto proc_root = get_root(false);
                if (current.dentry == proc_root.dentry && current.mnt == proc_root.mnt)
                {
                    if (last)
                        return resolve_res { current, current };
                    continue;
                }

                current = resolve_mounts(current);
                current.dentry = current.dentry->parent.lock();
                // current = resolve_mounts(current);

                if (current.mnt == nullptr)
                    current = proc_root;

                if (last)
                {
                    auto parent = resolve_mounts(current);
                    parent.dentry = parent.dentry->parent.lock();
                    parent = resolve_mounts(parent);
                    return resolve_res { parent, current };
                }
                continue;
            }

            if (!check_search(current))
                return std::unexpected { lib::err::permission_denied };

            auto dentry = current.dentry->children.lock()->lookup(segment);
            if (dentry != nullptr)
            {
                auto fs = current.mnt->fs.lock();
                if (fs.get() != nullptr && !fs->revalidate(dentry))
                {
                    current.dentry->children.lock()->erase(segment);
                    dentry = nullptr;
                }
            }
            if (dentry == nullptr)
            {
                auto found = [&] -> lib::expect<dir_entry> {
                    auto fs = current.mnt->fs.lock();
                    if (fs.get() == nullptr)
                        return std::unexpected { lib::err::io_error };
                    return fs->lookup(current.dentry, segment);
                } ();

                if (!found)
                    return std::unexpected { found.error() };

                dentry = current.dentry->children.lock()->lookup(segment);
                if (dentry == nullptr)
                {
                    const auto &entry = *found;

                    auto locked = current.dentry->children.lock();
                    dentry = locked->lookup(entry.name);
                    if (dentry == nullptr)
                    {
                        dentry = vfs::dentry_t::create();
                        dentry->parent = current.dentry;
                        dentry->name = entry.name;
                        dentry->inode = entry.inode;
                        locked->insert(dentry);
                    }
                }
            }

            auto mnt = current.mnt;

            // while (automount || !last)
            lib::unused(automount);
            while (true)
            {
                std::shared_ptr<struct mount_t> next_mnt;
                {
                    const auto cm_locked = dentry->child_mounts.lock();
                    for (const auto &child_mnt : *cm_locked)
                    {
                        const auto locked = child_mnt.lock();
                        if (!locked || !locked->mounted_on.has_value() || !locked->root)
                            continue;

                        if (mnt == locked->mounted_on->mnt)
                        {
                            next_mnt = locked;
                            break;
                        }
                    }
                }
                if (!next_mnt)
                    break;
                mnt = next_mnt;
                dentry = next_mnt->root;
            }

            path_t next { mnt, dentry };

            if (last)
                return resolve_res { current, next };

            auto type = next.dentry->inode->stat.type();
            if (type == stat::type::s_iflnk)
            {
                const auto reduced = reduce(current, next);
                if (!reduced)
                    return std::unexpected { reduced.error() };
                next = reduced.value();
                type = next.dentry->inode->stat.type();
            }

            if (type != stat::type::s_ifdir)
                return std::unexpected { lib::err::not_a_dir };

            current = next;
        }

        return std::unexpected { lib::err::not_found };
    }

    auto reduce(path_t parent, path_t src, bool automount, std::size_t symlink_depth)
        -> lib::expect<path_t>
    {
        const auto is_symlink = [&src]
        {
            const auto &dentry = src.dentry;
            return dentry->inode->stat.type() == stat::type::s_iflnk;
        };

        const auto og = symlink_depth;
        while (symlink_depth > 0)
        {
            if (!is_symlink())
                return src;

            if (src.mnt && (src.mnt->flags & ms_nosymfollow))
                return std::unexpected { lib::err::symloop_max };

            lib::path target;
            if (src.mnt)
            {
                auto fs = src.mnt->fs.lock();
                if (fs.get() == nullptr)
                    return std::unexpected { lib::err::io_error };
                auto link = fs->readlink(src.dentry);
                if (!link)
                    return std::unexpected { link.error() };
                target = std::move(*link);
            }
            else if (!src.dentry->symlinked_to.empty())
                target = src.dentry->symlinked_to;
            else
                return std::unexpected { lib::err::invalid_symlink };

            const auto ret = resolve(parent, target, automount);
            if (!ret)
                return std::unexpected { ret.error() };
            if (ret->target.dentry == src.dentry)
                return std::unexpected { lib::err::invalid_symlink };

            parent = ret->parent;
            src = ret->target;
            symlink_depth--;
        }

        if (og && symlink_depth == 0 && is_symlink())
            return std::unexpected { lib::err::symloop_max };

        return src;
    }

    auto mount(
        lib::path source_path, lib::path target_path,
        std::string_view fstype, std::uint64_t flags,
        std::optional<lib::maybe_uspan<const std::byte>> data
    ) -> lib::expect<void>
    {
        if (flags & ~std::to_underlying(ms_supported))
            return std::unexpected { lib::err::invalid_flags };

        if (flags & ms_remount)
        {
            auto ret = path_for(target_path);
            if (!ret)
                return std::unexpected { ret.error() };

            auto target = ret.value();
            if (!target.mnt || target.dentry != target.mnt->root)
                return std::unexpected { lib::err::invalid_path };

            target.mnt->flags = flags & ~std::to_underlying(ms_remount);

            if (!(flags & ms_silent))
                lib::info("vfs: remount('{}', 0x{:X})", target_path, flags);

            return { };
        }

        auto fs = find_fs(fstype);
        if (!fs)
            return std::unexpected { lib::err::invalid_filesystem };

        std::optional<path_t> source { };
        if (fs->requires_dev)
        {
            if (source_path.empty())
                return std::unexpected { lib::err::invalid_path };

            auto ret = path_for(source_path);
            if (!ret)
                return std::unexpected { ret.error() };

            source = ret.value();
            if (source->dentry->inode->stat.type() != stat::type::s_ifblk)
                return std::unexpected { lib::err::not_a_block };
        }

        auto ret = resolve_real_dir(std::nullopt, target_path);
        if (!ret)
            return std::unexpected { ret.error() };

        auto target = ret.value();
        if (target.dentry->inode->stat.type() != stat::type::s_ifdir)
            return std::unexpected { lib::err::not_a_dir };

        auto mnt = fs->mount(source ? source->dentry : std::shared_ptr<vfs::dentry_t> { }, data);
        if (!mnt)
            return std::unexpected { mnt.error() };

        mnt.value()->mounted_on = target;
        mnt.value()->id = allocate_mount_id();
        mnt.value()->parent_id = target.mnt ? target.mnt->id : 0;
        mnt.value()->flags = flags;
        mnt.value()->fstype = fstype;
        mnt.value()->source = source_path.str();
        target.dentry->child_mounts.lock()->push_back(mnt.value());
        (*mounts.lock())[mnt.value()->id] = mnt.value();

        if (!(flags & ms_silent))
        {
            lib::info(
                "vfs: mount('{}', '{}', '{}', 0x{:X})",
                source_path, target_path, fstype, flags
            );
        }

        return { };
    }

    auto unmount(lib::path target) -> lib::expect<void>
    {
        lib::unused(target);
        return std::unexpected { lib::err::todo };
    }

    auto create(
        std::optional<path_t> parent, lib::path _path,
        mode_t mode, dev_t rdev, std::shared_ptr<ops_t> ops
    ) -> lib::expect<path_t>
    {
        if (resolve(parent, _path))
            return std::unexpected { lib::err::already_exists };

        auto pres = resolve_real_dir(parent, _path.dirname());
        if (!pres)
            return std::unexpected { pres.error() };

        const auto real_parent = std::move(*pres);
        const auto name = _path.basename();

        std::optional<std::shared_ptr<struct ops_t>> _ops;
        if (!ops)
        {
            if (rdev != 0)
                _ops = nullptr;
        }
        else _ops = std::move(ops);

        auto ret = real_parent.mnt->fs.lock()->create(
            real_parent.dentry->inode, name, mode, rdev, std::move(_ops)
        );
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = vfs::dentry_t::create();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.lock()->insert(dentry);
        return path_t { real_parent.mnt, dentry };
    }

    auto symlink(std::optional<path_t> parent, lib::path src, lib::path target)
        -> lib::expect<path_t>
    {
        if (resolve(parent, src))
            return std::unexpected { lib::err::already_exists };

        auto pres = resolve_real_dir(parent, src.dirname());
        if (!pres)
            return std::unexpected { pres.error() };

        const auto real_parent = std::move(*pres);
        const auto name = src.basename();

        auto ret = real_parent.mnt->fs.lock()->symlink(real_parent.dentry->inode, name, target);
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = vfs::dentry_t::create();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->symlinked_to = target.str();
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.lock()->insert(dentry);
        return path_t { real_parent.mnt, dentry };
    }

    auto link(
        std::optional<path_t> parent, lib::path src,
        std::optional<path_t> tgtparent, lib::path target, bool follow_links
    ) -> lib::expect<path_t>
    {
        if (resolve(parent, src))
            return std::unexpected { lib::err::already_exists };

        auto pres = resolve_real_dir(parent, src.dirname());
        if (!pres)
            return std::unexpected { pres.error() };

        const auto real_parent = std::move(*pres);
        const auto name = src.basename();

        auto res = resolve(tgtparent, target);
        if (!res)
            return std::unexpected { res.error() };

        auto tgt = std::move(res->target);
        if (follow_links && tgt.dentry->inode->stat.type() == stat::s_iflnk)
        {
            auto reduced = reduce(res->parent, tgt);
            if (!reduced)
                return std::unexpected { reduced.error() };
            tgt = std::move(*reduced);
        }

        if (tgt.dentry->inode->stat.type() == stat::type::s_ifdir)
            return std::unexpected { lib::err::target_is_a_dir };

        if (real_parent.mnt != tgt.mnt)
            return std::unexpected { lib::err::different_filesystem };

        auto ret = real_parent.mnt->fs.lock()->link(
            real_parent.dentry->inode,
            name,
            tgt.dentry->inode
        );
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = vfs::dentry_t::create();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.lock()->insert(dentry);
        return path_t { real_parent.mnt, dentry };
    }

    auto unlink(std::optional<path_t> parent, lib::path path) -> lib::expect<void>
    {
        const auto base = path.basename();
        if (base == "." || base == "..")
            return std::unexpected { lib::err::invalid_path };

        const auto res = resolve(parent, path);
        if (!res)
            return std::unexpected { res.error() };

        const auto real_parent = res->parent.dentry;
        const auto target_dentry = res->target.dentry;

        if (target_dentry == real_parent)
            return std::unexpected { lib::err::invalid_path };

        {
            const auto locked = real_parent->children.lock();
            if (locked->lookup(target_dentry->name) != target_dentry)
                return std::unexpected { lib::err::invalid_path };
        }

        auto &inode = target_dentry->inode;
        if (inode->stat.type() == stat::s_ifdir)
        {
            if (target_dentry == res->target.mnt->root)
                return std::unexpected { lib::err::target_is_busy };

            if (!target_dentry->children.lock()->empty())
                return std::unexpected { lib::err::dir_not_empty };

            std::size_t cookie = 3;
            const auto ret = res->target.mnt->fs.lock()->readdir(target_dentry, cookie);
            if (!ret)
                return std::unexpected { ret.error() };
            if (!ret->empty())
                return std::unexpected { lib::err::dir_not_empty };
        }

        const auto ret = res->target.mnt->fs.lock()->unlink(inode);
        if (!ret)
            return std::unexpected { ret.error() };

        auto wlocked = real_parent->children.lock();
        lib::bug_on(!wlocked->erase(target_dentry->name));
        return { };
    }

    auto rename(
        std::optional<path_t> old_parent, lib::path old_path,
        std::optional<path_t> new_parent, lib::path new_path
    ) -> lib::expect<void>
    {
        const auto old_base = old_path.basename();
        const auto new_base = new_path.basename();
        if (old_base.str() == "."sv || old_base.str() == ".."sv ||
            new_base.str() == "."sv || new_base.str() == ".."sv)
            return std::unexpected { lib::err::invalid_path };

        const auto old_res = resolve(old_parent, old_path);
        if (!old_res)
            return std::unexpected { old_res.error() };

        const auto new_pres = resolve_real_dir(new_parent, new_path.dirname());
        if (!new_pres)
            return std::unexpected { new_pres.error() };

        if (old_res->parent.mnt != new_pres->mnt)
            return std::unexpected { lib::err::different_filesystem };

        const auto &old_dentry = old_res->target.dentry;
        const auto &old_parent_dentry = old_res->parent.dentry;
        const auto &new_parent_dentry = new_pres->dentry;

        if (old_dentry == old_res->target.mnt->root)
            return std::unexpected { lib::err::target_is_busy };

        const bool old_is_dir = old_dentry->inode->stat.type() == stat::type::s_ifdir;
        if (old_is_dir)
        {
            for (auto den = new_parent_dentry; den; )
            {
                if (den == old_dentry)
                    return std::unexpected { lib::err::invalid_path };
                auto parent = den->parent.lock();
                if (parent == den)
                    break;
                den = std::move(parent);
            }
        }

        const auto existing = resolve(new_parent, new_path);
        std::shared_ptr<dentry_t> new_dentry;
        if (existing.has_value())
        {
            new_dentry = existing->target.dentry;
            if (new_dentry == old_dentry || new_dentry->inode == old_dentry->inode)
                return { };

            const bool new_is_dir = new_dentry->inode->stat.type() == stat::type::s_ifdir;
            if (old_is_dir && !new_is_dir)
                return std::unexpected { lib::err::not_a_dir };
            if (!old_is_dir && new_is_dir)
                return std::unexpected { lib::err::target_is_a_dir };

            if (new_is_dir)
            {
                if (!new_dentry->children.lock()->empty())
                    return std::unexpected { lib::err::dir_not_empty };

                std::size_t cookie = 3;
                const auto rd = existing->target.mnt->fs.lock()->readdir(new_dentry, cookie);
                if (!rd)
                    return std::unexpected { rd.error() };
                if (!rd->empty())
                    return std::unexpected { lib::err::dir_not_empty };

                if (new_dentry == existing->target.mnt->root)
                    return std::unexpected { lib::err::target_is_busy };
            }
        }

        auto fs = old_res->parent.mnt->fs.lock();
        const auto do_rename = [&](auto &locked_old, auto &locked_new) -> lib::expect<void> {
            const auto ret = fs->rename(
                old_parent_dentry->inode, old_base.str(),
                new_parent_dentry->inode, new_base.str(),
                new_dentry ? new_dentry->inode : std::shared_ptr<inode_t> { }
            );
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            if (new_dentry)
                locked_new->erase(new_dentry->name);
            locked_old->erase(old_dentry->name);

            old_dentry->name = std::string { new_base.str() };
            old_dentry->parent = new_parent_dentry;
            locked_new->insert(old_dentry);
            return { };
        };

        if (old_parent_dentry == new_parent_dentry)
        {
            auto locked = old_parent_dentry->children.lock();
            return do_rename(locked, locked);
        }
        else if (old_parent_dentry.get() < new_parent_dentry.get())
        {
            auto locked_old = old_parent_dentry->children.lock();
            auto locked_new = new_parent_dentry->children.lock();
            return do_rename(locked_old, locked_new);
        }
        else
        {
            auto locked_new = new_parent_dentry->children.lock();
            auto locked_old = old_parent_dentry->children.lock();
            return do_rename(locked_old, locked_new);
        }
    }

    auto dirty_inode(const path_t &path) -> lib::expect<void>
    {
        if (path.mnt == nullptr)
            return { };

        auto fs = path.mnt->fs.lock();
        if (fs.get() == nullptr)
            return std::unexpected { lib::err::io_error };

        if (const auto ret = fs->dirty_inode(path.dentry->inode); !ret)
            return ret;

        return { };
    }

    auto getxattr(const path_t &target, std::string_view name) -> lib::expect<lib::membuffer>
    {
        lib::bug_on(!target.mnt);

        if (is_unsupported_xattr(name))
            return std::unexpected { lib::err::not_supported };

        const auto proc = sched::current_process();
        const auto &cred = proc->cred;

        if (!can_read_xattr(cred, name))
            return std::unexpected { lib::err::permission_denied };

        auto &inode = target.dentry->inode;
        const std::unique_lock _ { inode->lock };

        if (auto it = inode->xattrs.find(name); it != inode->xattrs.end())
            return it->second;

        auto fs = target.mnt->fs.lock();
        if (fs.get() == nullptr)
            return std::unexpected { lib::err::io_error };

        auto ret = fs->getxattr(inode, name);
        if (!ret.has_value())
            return ret;

        return inode->xattrs.insert_or_assign(name, std::move(*ret)).first->second;
    }

    auto setxattr(
        const path_t &target, std::string_view name,
        lib::maybe_uspan<std::byte> data, int flags
    ) -> lib::expect<void>
    {
        lib::bug_on(!target.mnt);

        if (is_unsupported_xattr(name))
            return std::unexpected { lib::err::not_supported };

        const auto proc = sched::current_process();
        const auto &cred = proc->cred;

        auto &inode = target.dentry->inode;
        const std::unique_lock _ { inode->lock };

        if (!can_write_xattr(cred, inode->stat, name))
            return std::unexpected { lib::err::permission_denied };

        if (inode->xattrs.contains(name))
        {
            if ((flags & xattr_create) != 0)
                return std::unexpected { lib::err::already_exists };
        }
        else if ((flags & xattr_replace) != 0)
            return std::unexpected { lib::err::does_not_exist };

        lib::membuffer buf { data.size() };
        if (!data.copy_to(buf.span()))
            return std::unexpected { lib::err::invalid_address };

        auto uspan = lib::maybe_uspan<std::byte>::create(buf.data(), buf.size());
        if (!uspan.has_value())
            return std::unexpected { lib::err::invalid_address };

        {
            auto fs = target.mnt->fs.lock();
            if (fs.get() == nullptr)
                return std::unexpected { lib::err::io_error };

            if (const auto ret = fs->setxattr(inode, name, *uspan, flags); !ret)
                return ret;
        }

        inode->xattrs.insert_or_assign(name, std::move(buf));

        inode->stat.update_time(kstat::time::status);
        return dirty_inode(target);
    }

    auto remxattr(const path_t &target, std::string_view name) -> lib::expect<void>
    {
        lib::bug_on(!target.mnt);

        if (is_unsupported_xattr(name))
            return std::unexpected { lib::err::not_supported };

        const auto proc = sched::current_process();
        const auto &cred = proc->cred;

        auto &inode = target.dentry->inode;
        const std::unique_lock _ { inode->lock };

        if (!can_write_xattr(cred, inode->stat, name))
            return std::unexpected { lib::err::permission_denied };

        if (!inode->xattrs.contains(name))
            return std::unexpected { lib::err::does_not_exist };

        {
            auto fs = target.mnt->fs.lock();
            if (fs.get() == nullptr)
                return std::unexpected { lib::err::io_error };

            if (const auto ret = fs->remxattr(inode, name); !ret)
                return ret;
        }

        inode->xattrs.erase(name);

        inode->stat.update_time(kstat::time::status);
        return dirty_inode(target);
    }

    auto listxattrs(const path_t &target) -> lib::expect<std::vector<std::string>>
    {
        auto &inode = target.dentry->inode;
        const std::unique_lock _ { inode->lock };

        if (inode->xattrs.empty())
        {
            lib::bug_on(!target.mnt);

            auto fs = target.mnt->fs.lock();
            if (fs.get() == nullptr)
                return std::unexpected { lib::err::io_error };

            auto ret = fs->listxattrs(inode);
            if (!ret.has_value())
                return ret;

            for (const auto &name : *ret)
            {
                if (auto val = fs->getxattr(inode, name); val.has_value())
                    inode->xattrs[name] = std::move(*val);
            }
        }

        return std::views::keys(inode->xattrs) |
            std::ranges::to<std::vector<std::string>>();
    }

    auto lenxattrs(const path_t &target) -> lib::expect<std::size_t>
    {
        auto &inode = target.dentry->inode;
        const std::unique_lock _ { inode->lock };

        if (inode->xattrs.empty())
        {
            lib::bug_on(!target.mnt);

            auto fs = target.mnt->fs.lock();
            if (fs.get() == nullptr)
                return std::unexpected { lib::err::io_error };

            auto ret = fs->listxattrs(inode);
            if (!ret.has_value())
                return std::unexpected { ret.error() };

            for (const auto &name : *ret)
            {
                if (auto val = fs->getxattr(inode, name))
                    inode->xattrs[name] = std::move(*val);
            }
        }

        return std::ranges::fold_left(
            std::views::keys(inode->xattrs) |
            std::views::transform([](const auto &name) {
                return name.size() + 1;
            }), 0, std::plus { }
        );
    }

    bool fdtable::close(int fd)
    {
        auto wlocked = fds.write_lock();
        if (!wlocked->erase(fd))
            return false;
        if (fd < next_fd)
            next_fd = fd;
        return true;
    }

    std::shared_ptr<vfs::filedesc> fdtable::get(int fd)
    {
        const auto rlocked = fds.read_lock();
        auto it = rlocked->find(fd);
        if (it == rlocked->end())
            return nullptr;
        return it->second;
    }

    lib::expect<int> fdtable::alloc(std::shared_ptr<vfs::filedesc> desc, int fd, bool force, rlim_t max_fd)
    {
        auto wlocked = fds.write_lock();
        if (wlocked->contains(fd))
        {
            if (!force)
            {
                fd = next_fd;
                while (wlocked->contains(fd))
                    fd++;
                next_fd = fd + 1;
            }
            else lib::bug_on(!wlocked->erase(fd));
        }

        if (static_cast<rlim_t>(fd) >= max_fd)
            return std::unexpected { lib::err::too_many_files };

        wlocked.value()[fd] = std::move(desc);
        return fd;
    }

    lib::expect<int> fdtable::dup(int oldfd, int newfd, bool closexec, bool force, rlim_t max_fd)
    {
        if (oldfd < 0 || newfd < 0)
            return std::unexpected { lib::err::invalid_fd };
        auto fdesc = get(oldfd);
        if (!fdesc)
            return std::unexpected { lib::err::invalid_fd };

        auto newfdesc = std::make_shared<vfs::filedesc>(fdesc->file, closexec);
        return alloc(std::move(newfdesc), newfd, force, max_fd);
    }

    void fdtable::close_on_exec()
    {
        auto wlocked = fds.write_lock();
        for (auto it = wlocked->begin(); it != wlocked->end(); )
        {
            if (it->second && it->second->closexec.load(std::memory_order_relaxed))
            {
                const auto closed_fd = it->first;
                it = wlocked->erase(it);
                if (closed_fd < next_fd)
                    next_fd = closed_fd;
            }
            else it++;
        }
    }

    std::shared_ptr<fdtable> fdtable::clone()
    {
        return std::make_shared<fdtable>(*this);
    }

    fdtable::fdtable(fdtable &other)
    {
        const auto orlocked = other.fds.read_lock();
        next_fd = other.next_fd;

        auto wlocked = fds.write_lock();
        for (const auto &[fd, old_desc] : *orlocked)
        {
            if (!old_desc)
                continue;

            wlocked.value()[fd] = std::make_shared<vfs::filedesc>(
                old_desc->file,
                old_desc->closexec.load(std::memory_order_relaxed)
            );
        }
    }

    auto create_anon_fd(const anon_fd_args &args)
        -> lib::expect<std::pair<int, std::shared_ptr<filedesc>>>
    {
        const auto proc = sched::current_process();
        auto &fdt = proc->fdt;

        std::shared_ptr<inode_t> inode;
        if (!args.inode)
        {
            inode = std::make_shared<vfs::inode_t>(args.ops);
            inode->stat.st_ino = allocate_anon_ino();
            inode->stat.st_blksize = 0x1000;
            inode->stat.st_mode = args.st_mode;
            inode->stat.st_uid = proc->cred->euid;
            inode->stat.st_gid = proc->cred->egid;
            inode->stat.update_time(
                kstat::time::access | kstat::time::modify |
                kstat::time::status | kstat::time::birth
            );
            inode->private_data = args.inode_private_data;
        }
        else inode = args.inode;

        auto dentry = vfs::dentry_t::create();
        dentry->name = args.name;
        dentry->inode = std::move(inode);

        auto fdesc = filedesc::create({
            .mnt = nullptr,
            .dentry = std::move(dentry)
        }, args.flags);
        fdesc->file->private_data = args.file_private_data;
        if (args.skip_open)
            fdesc->file->opened = true;

        const auto max_fd = proc->rlimits->get(sched::rlimit_nofile).cur;
        auto fdres = fdt->alloc(fdesc, 0, false, max_fd);
        if (!fdres)
            return std::unexpected { fdres.error() };
        const auto fd = *fdres;

        if (!args.skip_open)
        {
            if (const auto ret = fdesc->file->open(args.flags, proc->pid); !ret)
            {
                proc->fdt->close(fd);
                return std::unexpected { ret.error() };
            }
        }

        return std::make_pair(fd, std::move(fdesc));
    }

    lib::initgraph::stage *root_mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.root-mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    namespace
    {
        lib::initgraph::task root_task
        {
            "vfs.mount-root",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { fs::registered_stage() },
            lib::initgraph::entail { root_mounted_stage() },
            [] {
                lib::bug_on(!mount("", "/", "tmpfs", 0));
            }
        };

        std::shared_ptr<fs::procfs::node_ops> make_fdsym_ops(int fd)
        {
            return fs::procfs::make_symlink_ops(
                [fd](sched::process_t *proc) -> lib::expect<lib::path> {
                    if (!proc)
                        return std::unexpected { lib::err::not_found };

                    std::shared_ptr<vfs::filedesc> fdesc;
                    {
                        const std::unique_lock _ { proc->lock };
                        if (!proc->fdt)
                            return std::unexpected { lib::err::not_found };
                        fdesc = proc->fdt->get(fd);
                    }
                    if (!fdesc || !fdesc->file || !fdesc->file->path.dentry)
                        return std::unexpected { lib::err::not_found };

                    const auto &target_inode = fdesc->file->path.dentry->inode;
                    const auto target_ino = target_inode
                        ? target_inode->stat.st_ino : 0;
                    const auto type = target_inode
                        ? target_inode->stat.type() : stat::s_ifreg;

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
                },
                [fd](sched::process_t *proc) {
                    return proc->fdt->get(fd) != nullptr;
                }
            );
        };

        lib::initgraph::task procfs_register_task
        {
            "vfs.procfs.register-mounts",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { fs::procfs::registered_stage() },
            [] {
                using namespace fs::procfs;

                lib::bug_on(!register_global("mounts",
                    make_file_ops([](auto) {
                        const auto snapshot = *mounts.lock();
                        std::string out;
                        out.reserve(snapshot.size() * 96);
                        auto it = std::back_inserter(out);
                        for (const auto &[_, mnt] : snapshot)
                        {
                            if (!mnt->mounted_on || !mnt->mounted_on->dentry)
                                continue;
                            if (mnt->fstype.empty())
                                continue;

                            const auto mountpoint = pathname_from(*mnt->mounted_on);
                            if (mountpoint.empty())
                                continue;

                            const std::string_view source = mnt->source.empty()
                                ? std::string_view { mnt->fstype }
                                : mnt->source;

                            fmt::format_to(it, "{} {} {} {} 0 0\n",
                                source, mountpoint, mnt->fstype,
                                get_mnt_opts(mnt->flags, mnt->fs)
                            );
                        }
                        return out;
                    }), node_type::file, 0444
                ));

                lib::bug_on(!register_per_pid("mountinfo",
                    make_file_ops([](auto) {
                        const auto snapshot = *mounts.lock();
                        std::string out;
                        out.reserve(snapshot.size() * 128);
                        auto it = std::back_inserter(out);
                        for (const auto &[id, mnt] : snapshot)
                        {
                            if (!mnt->mounted_on || !mnt->mounted_on->dentry)
                                continue;
                            if (mnt->fstype.empty())
                                continue;

                            const auto mountpoint = pathname_from(*mnt->mounted_on);
                            if (mountpoint.empty())
                                continue;

                            const std::string_view source = mnt->source.empty()
                                ? std::string_view { mnt->fstype }
                                : mnt->source;

                            const auto dev_id = mnt->fs.lock()->dev_id;

                            // TODO: root within the fs, optional_fields, super_options
                            const auto mnt_opts = get_mnt_opts(mnt->flags, mnt->fs);
                            fmt::format_to(it, "{} {} 0:{} {} {} {} - {} {} {}\n",
                                mnt->id, mnt->parent_id, dev_id, "/", mountpoint,
                                mnt_opts, mnt->fstype, source, mnt_opts
                            );
                        }
                        return out;
                    }), node_type::file, 0444
                ));

                lib::bug_on(!register_global("filesystems",
                    make_file_ops([](auto) {
                        const auto locked = filesystems.lock();
                        std::string out;
                        out.reserve(locked->size() * 32);
                        auto it = std::back_inserter(out);
                        for (const auto &[name, fs] : *locked)
                        {
                            fmt::format_to(it, "{}\t{}\n",
                                !fs->requires_dev ? "nodev" : "", name
                            );
                        }
                        return out;
                    }), node_type::file, 0444
                ));

                lib::bug_on(!register_per_pid("fd",
                    make_dir_ops(
                        [](sched::process_t *proc, std::string_view name) -> lib::expect<node_t> {
                            if (!proc)
                                return std::unexpected { lib::err::not_found };

                            char *end;
                            const auto res = lib::str2int<int>(name.data(), &end, 10);
                            if (!res.has_value() || end != name.data() + name.size())
                                return std::unexpected { lib::err::not_found };
                            const auto fd = *res;

                            auto file = proc->fdt->get(fd);
                            if (!file)
                                return std::unexpected { lib::err::not_found };

                            return node_t {
                                .name = std::string { name },
                                .mode = 0777,
                                .type = node_type::symlink,
                                .ops = make_fdsym_ops(fd)
                            };
                        },
                        [](sched::process_t *proc) -> lib::expect<lib::list<node_t>> {
                            if (!proc)
                                return std::unexpected { lib::err::not_found };

                            lib::list<node_t> result;
                            for (const auto &[fd, _] : *proc->fdt->fds.read_lock())
                            {
                                result.push_back(node_t {
                                    .name = std::to_string(fd),
                                    .mode = 0777,
                                    .type = node_type::symlink,
                                    .ops = make_fdsym_ops(fd)
                                });
                            };
                            return result;
                        }
                    ), node_type::dir, 0555
                ));
            }
        };
    } // namespace
} // namespace vfs
