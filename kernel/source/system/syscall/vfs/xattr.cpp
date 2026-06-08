// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        lib::expect<path> xattr_target_path(
            sched::process_t *proc, const char __user *pathname, bool follow_links
        )
        {
            return get_target(proc, at_fdcwd, pathname, follow_links, false, true);
        }

        lib::expect<path> xattr_target_fd(sched::process_t *proc, int fd)
        {
            auto fdesc = detail::get_fd(proc, fd);
            if (!fdesc)
                return std::unexpected { fdesc.error() };
            return (*fdesc)->file->path;
        }

        lib::expect<std::string> xattr_name(const char __user *name)
        {
            if (name == nullptr)
                return std::unexpected { lib::err::invalid_address };
            auto str = lib::user_string::get(name, xattr_name_max + 1);
            if (!str.has_value())
                return std::unexpected { lib::err::invalid_path };
            return std::move(*str);
        }

        int do_setxattr(
            const path &target, std::string_view name, const void __user *value, std::size_t size,
            int flags
        )
        {
            if (size > xattr_size_max)
                return -E2BIG;
            if (detail::readonly_mount(target))
                return -EROFS;

            auto uspan =
                lib::maybe_uspan<std::byte>::create(const_cast<void __user *>(value), size);
            if (!uspan.has_value())
                return -EFAULT;

            if (const auto ret = setxattr(target, name, *uspan, flags); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }

        std::ssize_t do_getxattr(
            const path &target, std::string_view name, void __user *value, std::size_t size
        )
        {
            auto ret = getxattr(target, name);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            const auto buf_size = ret->size();
            if (size == 0)
                return static_cast<std::ssize_t>(buf_size);
            if (size < buf_size)
                return -ERANGE;

            if (buf_size > 0 && !lib::copy_to_user(value, ret->data(), buf_size))
                return -EFAULT;
            return static_cast<std::ssize_t>(buf_size);
        }

        std::ssize_t do_listxattr(const path &target, char __user *list, std::size_t size)
        {
            if (size == 0)
            {
                auto ret = lenxattrs(target);
                if (!ret.has_value())
                    return -lib::map_error(ret.error());
                return static_cast<std::ssize_t>(*ret);
            }

            auto ret = listxattrs(target);
            if (!ret.has_value())
                return -lib::map_error(ret.error());

            std::size_t total = 0;
            for (const auto &name : *ret)
                total += name.size() + 1;

            if (size < total)
                return -ERANGE;

            std::vector<char> buf;
            buf.reserve(total);
            for (const auto &name : *ret)
            {
                buf.insert(buf.end(), name.begin(), name.end());
                buf.push_back('\0');
            }

            if (total > 0 && !lib::copy_to_user(list, buf.data(), total))
                return -EFAULT;
            return static_cast<std::ssize_t>(total);
        }

        int do_removexattr(const path &target, std::string_view name)
        {
            if (detail::readonly_mount(target))
                return -EROFS;
            if (const auto ret = remxattr(target, name); !ret)
                return -lib::map_error(ret.error());
            return 0;
        }
    } // namespace

    int setxattr(
        const char __user *pathname, const char __user *name, const void __user *value,
        std::size_t size, int flags
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_setxattr(*target, *kname, value, size, flags);
    }

    int lsetxattr(
        const char __user *pathname, const char __user *name, const void __user *value,
        std::size_t size, int flags
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_setxattr(*target, *kname, value, size, flags);
    }

    int fsetxattr(
        int fd, const char __user *name, const void __user *value, std::size_t size, int flags
    )
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_setxattr(*target, *kname, value, size, flags);
    }

    std::ssize_t getxattr(
        const char __user *pathname, const char __user *name, void __user *value, std::size_t size
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_getxattr(*target, *kname, value, size);
    }

    std::ssize_t lgetxattr(
        const char __user *pathname, const char __user *name, void __user *value, std::size_t size
    )
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_getxattr(*target, *kname, value, size);
    }

    std::ssize_t fgetxattr(int fd, const char __user *name, void __user *value, std::size_t size)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_getxattr(*target, *kname, value, size);
    }

    std::ssize_t listxattr(const char __user *pathname, char __user *list, std::size_t size)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        return do_listxattr(*target, list, size);
    }

    std::ssize_t llistxattr(const char __user *pathname, char __user *list, std::size_t size)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        return do_listxattr(*target, list, size);
    }

    std::ssize_t flistxattr(int fd, char __user *list, std::size_t size)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        return do_listxattr(*target, list, size);
    }

    int removexattr(const char __user *pathname, const char __user *name)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_removexattr(*target, *kname);
    }

    int lremovexattr(const char __user *pathname, const char __user *name)
    {
        const auto target = xattr_target_path(sched::current_process(), pathname, false);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_removexattr(*target, *kname);
    }

    int fremovexattr(int fd, const char __user *name)
    {
        const auto target = xattr_target_fd(sched::current_process(), fd);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto kname = xattr_name(name);
        if (!kname.has_value())
            return -lib::map_error(kname.error());

        return do_removexattr(*target, *kname);
    }
} // namespace syscall::vfs
