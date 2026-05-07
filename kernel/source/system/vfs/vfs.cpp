// Copyright (C) 2024-2026  ilobilo

module system.vfs;

import system.cpu.local;
import system.vfs.dev;
import system.sched;
import drivers.fs;
import lib;
import std;

namespace vfs
{
    namespace
    {
        std::shared_ptr<dentry> root = [] {
            auto root = std::make_shared<dentry>();
            root->name = "/";
            root->parent = root;
            root->inode = std::make_shared<inode>();
            root->inode->stat.st_mode = static_cast<mode_t>(stat::type::s_ifdir) |
                (s_irwxu | s_irgrp | s_ixgrp | s_iroth | s_ixoth);
            return root;
        } ();

        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                std::unique_ptr<filesystem>
            >, sched::mutex
        > filesystems;

        std::atomic<dev_t> next_dev = 1;
        dev_t allocate_dev()
        {
            return next_dev.fetch_add(1, std::memory_order_relaxed);
        }

        path resolve_mounts(path path)
        {
            while (path.mnt != nullptr && path.dentry == path.mnt->root)
            {
                if (!path.mnt->mounted_on.has_value())
                    break;
                path = path.mnt->mounted_on.value();
            }
            return path;
        };


        auto resolve_real_dir(std::optional<path> anchor, lib::path dir) -> lib::expect<path>
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
    } // namespace

    filesystem::instance::instance() : dev_id { allocate_dev() } { }

    auto file::get_ops() const -> lib::expect<std::shared_ptr<ops>>
    {
        lib::bug_on(!path.dentry || !path.dentry->inode);

        auto &inode = path.dentry->inode;
        const auto &stat = inode->stat;

        const auto dev = stat.st_dev;
        const auto rdev = stat.st_rdev;
        const auto mode = stat.st_mode;

        auto ops = dev::get_ops(dev, rdev, mode);
        if (!ops.has_value())
            return std::unexpected { ops.error() };

        return *ops;
    }

    lib::expect<std::size_t> file::getdents(lib::maybe_uspan<std::byte> buffer)
    {
        const auto ops = get_ops();
        if (!ops.has_value())
            return std::unexpected { ops.error() };

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

    auto filesystem::instance::lookup(std::shared_ptr<dentry> dir, std::string_view name)
        -> lib::expect<std::optional<dir_entry>>
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
                return std::nullopt;

            auto locked = dir->children.lock();
            for (auto &entry : *batch)
            {
                if (!locked->lookup(entry.name))
                {
                    auto dentry = std::make_shared<vfs::dentry>();
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

    path get_root(bool absolute)
    {
        if (!absolute)
        {
            auto proc = sched::current_process();
            if (proc->vfs) [[likely]]
                return proc->vfs->root;
        }

        path ret { .mnt = nullptr, .dentry = dentry::root(true) };
        while (true)
        {
            std::shared_ptr<struct mount> mnt;
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

    bool register_fs(std::unique_ptr<filesystem> fs)
    {
        auto locked = filesystems.lock();
        if (locked->contains(fs->name))
            return false;

        lib::info("vfs: registering filesystem '{}'", fs->name);
        locked.value()[fs->name] = std::move(fs);
        return true;
    }

    auto find_fs(std::string_view name)
        -> lib::expect<std::reference_wrapper<std::unique_ptr<filesystem>>>
    {
        auto locked = filesystems.lock();
        if (auto it = locked->find(name); it != locked->end())
            return it->second;
        return std::unexpected { lib::err::invalid_filesystem };
    }

    std::shared_ptr<dentry> dentry::root(bool absolute)
    {
        if (!absolute)
        {
            auto proc = sched::current_process();
            if (proc->vfs) [[likely]]
                return proc->vfs->root.dentry;
        }
        return vfs::root;
    }

    std::string pathname_from(path path)
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

    auto path_for(lib::path _path) -> lib::expect<path>
    {
        // hmmm is this correct?
        auto res = resolve(std::nullopt, _path);
        if (!res)
            return std::unexpected { res.error() };
        return res->target;
    }

    auto resolve(std::optional<path> parent, lib::path _path, bool automount)
        -> lib::expect<resolve_res>
    {
        if (!parent || _path.is_absolute())
            parent = get_root(false);

        lib::bug_on(!parent.has_value());

        if (_path.str() == "/"sv || _path.empty() || _path.str() == "."sv)
            return resolve_res { parent.value(), parent.value() };

        lib::bug_on(parent->mnt == nullptr);

        auto check_search = [&](const auto &parent) {
            const auto &stat = parent->inode->stat;
            return sched::check_perms(stat, sched::access_mode::exec);
        };

        auto current = parent.value();

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
                current = resolve_mounts(current);

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

            if (!check_search(current.dentry))
                return std::unexpected { lib::err::permission_denied };

            auto dentry = current.dentry->children.lock()->lookup(segment);
            if (dentry == nullptr)
            {
                auto found = [&] -> lib::expect<std::optional<dir_entry>> {
                    auto fs = current.mnt->fs.lock();
                    if (fs.get() == nullptr)
                        return std::unexpected { lib::err::io_error };
                    return fs->lookup(current.dentry, segment);
                } ();

                if (!found)
                    return std::unexpected { found.error() };

                if (!found->has_value())
                    return std::unexpected { lib::err::not_found };

                dentry = current.dentry->children.lock()->lookup(segment);
                if (dentry == nullptr)
                {
                    const auto &entry = *found;

                    auto locked = current.dentry->children.lock();
                    dentry = locked->lookup(entry->name);
                    if (dentry == nullptr)
                    {
                        dentry = std::make_shared<vfs::dentry>();
                        dentry->parent = current.dentry;
                        dentry->name = entry->name;
                        dentry->inode = entry->inode;
                        locked->insert(dentry);
                    }
                }
            }

            auto mnt = current.mnt;

            while (automount || !last)
            {
                std::shared_ptr<struct mount> next_mnt;
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

            path next { mnt, dentry };

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

    auto reduce(path parent, path src, bool automount, std::size_t symlink_depth)
        -> lib::expect<path>
    {
        const auto is_symlink = [&src]
        {
            const auto &dentry = src.dentry;
            return
                dentry->inode->stat.type() == stat::type::s_iflnk &&
                !dentry->symlinked_to.empty();
        };

        const auto og = symlink_depth;
        while (symlink_depth > 0)
        {
            if (!is_symlink())
                return src;

            if (src.mnt && (src.mnt->flags & ms_nosymfollow))
                return std::unexpected { lib::err::symloop_max };

            const auto ret = resolve(parent, src.dentry->symlinked_to, automount);
            if (!ret || ret->target.dentry == src.dentry)
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
        std::string_view fstype, unsigned long flags,
        std::optional<lib::maybe_uspan<const std::byte>> data
    ) -> lib::expect<void>
    {
        if (flags & ~static_cast<unsigned long>(ms_supported))
            return std::unexpected { lib::err::invalid_flags };

        if (flags & ms_remount)
        {
            auto ret = path_for(target_path);
            if (!ret)
                return std::unexpected { ret.error() };

            auto target = ret.value();
            if (!target.mnt || target.dentry != target.mnt->root)
                return std::unexpected { lib::err::invalid_path };

            target.mnt->flags = flags & ~static_cast<unsigned long>(ms_remount);

            if (!(flags & ms_silent))
                lib::info("vfs: remount('{}', 0x{:X})", target_path, flags);

            return { };
        }

        auto fs = find_fs(fstype);
        if (!fs)
            return std::unexpected { fs.error() };

        std::optional<path> source { };
        if (fs->get()->requires_dev)
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

        auto ret = path_for(target_path);
        if (!ret)
            return std::unexpected { ret.error() };

        auto target = ret.value();
        if (target.dentry->inode->stat.type() != stat::type::s_ifdir)
            return std::unexpected { lib::err::not_a_dir };

        auto mnt = fs->get()->mount(source ? source->dentry : std::shared_ptr<vfs::dentry> { }, data);
        if (!mnt)
            return std::unexpected { mnt.error() };

        mnt.value()->mounted_on = target;
        mnt.value()->flags = flags;
        target.dentry->child_mounts.lock()->push_back(mnt.value());

        if (!(flags & ms_silent))
            lib::info("vfs: mount('{}', '{}', '{}', 0x{:X})", source_path, target_path, fstype, flags);

        return { };
    }

    auto unmount(lib::path target) -> lib::expect<void>
    {
        lib::unused(target);
        return std::unexpected { lib::err::todo };
    }

    auto create(std::optional<path> parent, lib::path _path, mode_t mode, dev_t rdev)
        -> lib::expect<path>
    {
        if (resolve(parent, _path))
            return std::unexpected { lib::err::already_exists };

        auto pres = resolve_real_dir(parent, _path.dirname());
        if (!pres)
            return std::unexpected { pres.error() };

        const auto real_parent = std::move(*pres);
        const auto name = _path.basename();

        auto ret = real_parent.mnt->fs.lock()->create(real_parent.dentry->inode, name, mode, rdev);
        if (!ret)
            return std::unexpected { ret.error() };

        auto dentry = std::make_shared<vfs::dentry>();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.lock()->insert(dentry);
        return path { real_parent.mnt, dentry };
    }

    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> lib::expect<path>
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

        auto dentry = std::make_shared<vfs::dentry>();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->symlinked_to = target.str();
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.lock()->insert(dentry);
        return path { real_parent.mnt, dentry };
    }

    auto link(
        std::optional<path> parent, lib::path src,
        std::optional<path> tgtparent, lib::path target, bool follow_links
    ) -> lib::expect<path>
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

        auto dentry = std::make_shared<vfs::dentry>();
        dentry->parent = real_parent.dentry;
        dentry->name = name;
        dentry->inode = std::move(*ret);

        real_parent.dentry->children.lock()->insert(dentry);
        return path { real_parent.mnt, dentry };
    }

    auto unlink(std::optional<path> parent, lib::path path) -> lib::expect<void>
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
        std::optional<path> old_parent, lib::path old_path,
        std::optional<path> new_parent, lib::path new_path
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
        std::shared_ptr<dentry> new_dentry;
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
                new_dentry ? new_dentry->inode : std::shared_ptr<inode> { }
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

    auto dirty_inode(const path &path) -> lib::expect<void>
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

    auto getxattr(const path &target, std::string_view name) -> lib::expect<lib::membuffer>
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
        const path &target, std::string_view name,
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

        {
            auto fs = target.mnt->fs.lock();
            if (fs.get() == nullptr)
                return std::unexpected { lib::err::io_error };

            if (const auto ret = fs->setxattr(inode, name, data, flags); !ret)
                return ret;
        }

        lib::membuffer buf { data.size() };
        const bool copied = data.copy_to(buf.span());
        inode->xattrs.insert_or_assign(name, std::move(buf));

        inode->stat.update_time(kstat::time::status);
        if (const auto ret = dirty_inode(target); !ret)
            return ret;

        if (!copied)
            return std::unexpected { lib::err::invalid_address };
        return { };
    }

    auto remxattr(const path &target, std::string_view name) -> lib::expect<void>
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

    auto listxattrs(const path &target) -> lib::expect<std::vector<std::string>>
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
                if (auto val = fs->getxattr(inode, name); !val.has_value())
                    inode->xattrs[name] = std::move(*val);
            }
        }

        return std::views::keys(inode->xattrs) |
            std::ranges::to<std::vector<std::string>>();
    }

    auto lenxattrs(const path &target) -> lib::expect<std::size_t>
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

    int fdtable::alloc(std::shared_ptr<vfs::filedesc> desc, int fd, bool force, rlim_t max_fd)
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
            return -EMFILE;

        wlocked.value()[fd] = std::move(desc);
        return fd;
    }

    int fdtable::dup(int oldfd, int newfd, bool closexec, bool force, rlim_t max_fd)
    {
        if (oldfd < 0 || newfd < 0)
            return -EBADF;
        auto fdesc = get(oldfd);
        if (!fdesc)
            return -EBADF;

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
        auto orlocked = other.fds.read_lock();
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

lib::initgraph::stage *root_mounted_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.root-mounted",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

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
} // namespace vfs
