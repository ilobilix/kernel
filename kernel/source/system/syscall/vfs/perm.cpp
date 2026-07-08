// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import magic_enum;

namespace syscall::vfs
{
    using namespace ::vfs;
    using namespace magic_enum::bitwise_operators;

    mode_t umask(mode_t mask)
    {
        const auto proc = sched::current_process();
        const auto ret = proc->vfs->umask;
        proc->vfs->umask = mask & 0777;
        return ret;
    }

    int faccessat2(int dirfd, const char __user *pathname, int mode, int flags)
    {
        if ((mode & ~(f_ok | x_ok | w_ok | r_ok)) != 0)
            return -EINVAL;

        if ((flags & ~(at_symlink_nofollow | at_empty_path | at_eaccess)) != 0)
            return -EINVAL;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;
        const bool eaccess = (flags & at_eaccess) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (mode == f_ok)
            return 0;

        using namespace magic_enum::bitwise_operators;
        auto desired = sched::access_mode::none;
        if (mode & r_ok)
            desired |= sched::access_mode::read;
        if (mode & w_ok)
            desired |= sched::access_mode::write;
        if (mode & x_ok)
            desired |= sched::access_mode::exec;

        std::shared_ptr<sched::cred_t> cred;
        if (eaccess == false)
        {
            cred = proc->cred->clone();
            cred->fsuid = cred->ruid;
            cred->fsgid = cred->rgid;

            if (cred->ruid != 0)
                cred->effective &= ~(sched::cap_t::dac_override | sched::cap_t::dac_read_search);
        }
        else cred = proc->cred;

        if (!vfs::check_access(*target, cred, static_cast<std::uint32_t>(desired)))
            return -EACCES;

        return 0;
    }

    int faccessat(int dirfd, const char __user *pathname, int mode)
    {
        return faccessat2(dirfd, pathname, mode, 0);
    }

    int access(const char __user *pathname, int mode)
    {
        return faccessat2(at_fdcwd, pathname, mode, 0);
    }

    int fchmodat2(int dirfd, const char __user *pathname, mode_t mode, int flags)
    {
        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return -EINVAL;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (detail::readonly_mount(*target))
            return -EROFS;

        auto &inode = target->dentry->inode;
        {
            const std::unique_lock _ { inode->lock };

            const auto &cred = proc->cred;
            if (cred->fsuid != inode->stat.st_uid && !sched::capable(cred, sched::cap_t::fowner))
                return -EPERM;

            if ((mode & s_isgid) && cred->fsgid != inode->stat.st_gid &&
                !cred->supp_gids.contains(inode->stat.st_gid) &&
                !sched::capable(cred, sched::cap_t::fsetid))
                mode &= ~s_isgid;

            constexpr auto bits = (s_irwxu | s_irwxg | s_irwxo | s_isvtx | s_isuid | s_isgid);

            inode->stat.st_mode = (inode->stat.st_mode & ~bits) | (mode & bits);
            inode->stat.update_time(kstat::time::status);

            if (const auto ret = dirty_inode(*target); !ret)
                return -lib::map_error(ret.error());
        }
        return 0;
    }

    int fchmodat(int dirfd, const char __user *pathname, mode_t mode)
    {
        return fchmodat2(dirfd, pathname, mode, 0);
    }

    int chmod(const char __user *pathname, mode_t mode)
    {
        return fchmodat2(at_fdcwd, pathname, mode, 0);
    }

    int fchmod(int fd, mode_t mode)
    {
        return fchmodat2(fd, nullptr, mode, at_empty_path);
    }

    int fchownat(int dirfd, const char __user *pathname, uid_t owner, gid_t group, int flags)
    {
        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return -EINVAL;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        if (detail::readonly_mount(*target))
            return -EROFS;

        auto &inode = target->dentry->inode;
        {
            const std::unique_lock _ { inode->lock };

            const bool change_uid = (owner != static_cast<uid_t>(-1));
            const bool change_gid = (group != static_cast<gid_t>(-1));

            if (change_uid || change_gid)
            {
                const auto &cred = proc->cred;

                const bool uid_noop = !change_uid || (owner == inode->stat.st_uid);
                const bool gid_noop = !change_gid || (group == inode->stat.st_gid);

                const bool gid_allowed = gid_noop ||
                    (cred->fsgid == group || cred->supp_gids.contains(group));

                const bool is_owner = (cred->fsuid == inode->stat.st_uid);
                const bool allow_unpriv = is_owner && uid_noop && gid_allowed;

                if (!allow_unpriv && !sched::capable(cred, sched::cap_t::chown))
                    return -EPERM;

                const auto old_uid = inode->stat.st_uid;
                const auto old_gid = inode->stat.st_gid;
                const auto old_mode = inode->stat.st_mode;

                if (change_uid && !uid_noop && inode->xattrs.contains(xattr_caps_name))
                {
                    lib::bug_on(!target->mnt);

                    auto fs = target->mnt->fs.lock();
                    if (!fs.get())
                        return -EIO;

                    if (const auto ret = fs->remxattr(inode, xattr_caps_name); !ret)
                        return -lib::map_error(ret.error());

                    inode->xattrs.erase(xattr_caps_name);
                }

                if (change_uid)
                    inode->stat.st_uid = owner;
                if (change_gid)
                    inode->stat.st_gid = group;

                if (!sched::capable(cred, sched::cap_t::fsetid))
                {
                    inode->stat.st_mode &= ~s_isuid;
                    if (inode->stat.st_mode & s_ixgrp)
                        inode->stat.st_mode &= ~s_isgid;
                }

                inode->stat.update_time(kstat::time::status);
                if (const auto ret = dirty_inode(*target); !ret)
                {
                    // TODO: restore xattr
                    inode->stat.st_uid = old_uid;
                    inode->stat.st_gid = old_gid;
                    inode->stat.st_mode = old_mode;
                    return -lib::map_error(ret.error());
                }
            }
        }
        return 0;
    }

    int chown(const char __user *pathname, uid_t owner, gid_t group)
    {
        return fchownat(at_fdcwd, pathname, owner, group, 0);
    }

    int fchown(int fd, uid_t owner, gid_t group)
    {
        return fchownat(fd, nullptr, owner, group, at_empty_path);
    }

    int lchown(const char __user *pathname, uid_t owner, gid_t group)
    {
        return fchownat(at_fdcwd, pathname, owner, group, at_symlink_nofollow);
    }
} // namespace syscall::vfs
