// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.chrono;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace detail
    {
        lib::expect<std::shared_ptr<filedesc>> get_fd(sched::process_t *proc, int fdnum)
        {
            if (fdnum < 0)
                return std::unexpected { lib::err::invalid_fd };
            if (auto fd = proc->fdt->get(fdnum))
                return fd;
            return std::unexpected { lib::err::invalid_fd };
        }

        lib::expect<path_t> get_parent(sched::process_t *proc, int dirfd, lib::path_view path)
        {
            if (path.is_absolute())
                return get_root(true);

            if (dirfd == at_fdcwd)
                return proc->vfs->cwd;

            auto fd = get_fd(proc, dirfd);
            if (!fd)
                return std::unexpected { fd.error() };

            if ((*fd)->file->path.dentry->inode->stat.type() != stat::type::s_ifdir)
                return std::unexpected { lib::err::not_a_dir };

            return (*fd)->file->path;
        }

        lib::expect<resolve_res> resolve_from(
            sched::process_t *proc, int dirfd,
            lib::path_view path, bool automount
        )
        {
            auto parent = get_parent(proc, dirfd, path);
            if (!parent)
                return std::unexpected { parent.error() };

            auto res = resolve(std::move(*parent), path, automount);
            if (!res)
                return std::unexpected { res.error() };

            return std::move(*res);
        }

        lib::expect<path_t> resolve_parent_dir(
            sched::process_t *proc, int dirfd, lib::path_view path
        )
        {
            auto res = resolve_from(proc, dirfd, path);
            if (!res.has_value())
                return std::unexpected { res.error() };

            if (res->target.dentry->inode->stat.type() == stat::type::s_iflnk)
            {
                auto reduced = reduce(res->parent, res->target);
                if (!reduced.has_value())
                    return std::unexpected { reduced.error() };
                return std::move(*reduced);
            }
            return std::move(res->target);
        }

        lib::expect<lib::path> get_path(const char __user *pathname)
        {
            if (pathname == nullptr)
                return std::unexpected { lib::err::invalid_address };

            const auto pathname_len = lib::strnlen_user(pathname, path_max);
            if (pathname_len < 0)
                return std::unexpected { lib::err::invalid_address };
            if (pathname_len == 0)
                return std::unexpected { lib::err::invalid_path };
            if (pathname_len == path_max)
                return std::unexpected { lib::err::path_too_long };

            lib::path path { static_cast<std::size_t>(pathname_len), 0 };
            lib::bug_on(path.str().size() != static_cast<std::size_t>(pathname_len));

            if (!lib::copy_from_user(path.str().data(), pathname, pathname_len))
                return std::unexpected { lib::err::invalid_address };

            path.normalise();
            return path;
        }

        std::uint64_t mount_flags(const path_t &path)
        {
            return path.mnt ? path.mnt->flags : 0ul;
        }

        bool readonly_mount(const path_t &path)
        {
            return (mount_flags(path) & ms_rdonly) != 0;
        }

        bool should_update_atime(const path_t &path, const kstat &stat, int file_flags)
        {
            if (file_flags & o_noatime)
                return false;

            const auto flags = mount_flags(path);
            if (flags & ms_noatime)
                return false;
            if ((flags & ms_nodiratime) && stat.type() == stat::type::s_ifdir)
                return false;
            if (flags & ms_strictatime)
                return true;

            const auto &atim = stat.st_atim;
            if (atim < stat.st_mtim || atim < stat.st_ctim)
                return true;

            constexpr timespec day { 24 * 60 * 60, 0 };
            return chrono::now(chrono::realtime) - atim >= day;
        }

        int touch_atime(const std::shared_ptr<vfs::file_t> &file)
        {
            auto &inode = file->path.dentry->inode;
            const std::unique_lock _ { inode->lock };
            if (!should_update_atime(file->path, inode->stat, file->flags))
                return 0;

            inode->stat.update_time(kstat::time::access);
            if (const auto ret = dirty_inode(file->path); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }
    } // namespace detail

    lib::expect<path_t> get_target(
        sched::process_t *proc, int dirfd, const char __user *pathname,
        bool follow_links, bool empty_path, bool automount
    )
    {
        if (empty_path)
        {
            bool use_dirfd = (pathname == nullptr);
            if (!use_dirfd)
            {
                const auto pathname_len = lib::strnlen_user(pathname, 1);
                if (pathname_len < 0)
                    return std::unexpected { lib::err::invalid_address };
                use_dirfd = (pathname_len == 0);
            }

            if (use_dirfd)
            {
                if (dirfd == at_fdcwd)
                    return proc->vfs->cwd;

                auto fd = detail::get_fd(proc, dirfd);
                if (!fd)
                    return std::unexpected { fd.error() };

                return (*fd)->file->path;
            }
        }

        auto val = detail::get_path(pathname);
        if (!val)
            return std::unexpected { val.error() };

        auto res = detail::resolve_from(proc, dirfd, std::move(*val), automount);
        if (!res)
            return std::unexpected { res.error() };

        auto target = std::move(res->target);
        lib::bug_on(!target.dentry || !target.dentry->inode);

        if (follow_links)
        {
            auto reduced = reduce(std::move(res->parent), std::move(target), automount);
            if (!reduced)
                return std::unexpected { reduced.error() };
            target = std::move(*reduced);
        }
        return target;
    }
} // namespace syscall::vfs
