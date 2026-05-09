// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

namespace syscall::vfs
{
    using namespace ::vfs;

    int mount(
        const char __user *source, const char __user *target,
        const char __user *fstype, std::uint64_t flags, const void __user *data
    )
    {
        const auto proc = sched::current_process();

        if (!sched::capable(proc->cred, sched::cap_t::sys_admin))
            return -EPERM;

        if (target == nullptr)
            return -EFAULT;

        auto target_val = detail::get_path(target);
        if (!target_val.has_value())
            return -lib::map_error(target_val.error());

        // TODO
        if (flags & (ms_shared | ms_private | ms_slave | ms_unbindable))
            return 0;

        std::optional<std::string> fstype_str;
        if (fstype != nullptr)
        {
            fstype_str = lib::user_string::get(fstype, 256);
            if (!fstype_str.has_value())
                return -EINVAL;
        }
        else if (!(flags & ms_remount))
            return -EFAULT;

        lib::path source_path { };
        if (source != nullptr)
        {
            auto val = detail::get_path(source);
            if (val.has_value())
                source_path = std::move(*val);
            else if (val.error() != lib::err::invalid_path)
                return -lib::map_error(val.error());
        }

        constexpr std::size_t data_max = 4096;
        std::array<std::byte, data_max> data_buf { };
        std::optional<lib::maybe_uspan<const std::byte>> data_uspan { };
        if (data != nullptr)
        {
            const auto probe = lib::strnlen_user(static_cast<const char __user *>(data), data_max);
            if (probe < 0)
                return -EFAULT;

            if (probe > 0 && !lib::copy_from_user(data_buf.data(), data, probe))
                return -EFAULT;

            const auto uspan = lib::maybe_uspan<const std::byte>::create(data_buf.data(), data_max);
            if (!uspan.has_value())
                return -EFAULT;
            data_uspan = *uspan;
        }

        const std::string_view fstype_sv = fstype_str ? *fstype_str : std::string_view { };
        if (const auto ret = ::vfs::mount(source_path, *target_val, fstype_sv, flags, data_uspan); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int fsync(int fd)
    {
        // TODO
        lib::unused(fd);
        return 0;
    }

    int fsopen(const char __user *fsname, std::uint32_t flags)
    {
        // TODO
        lib::unused(fsname, flags);
        return -ENOSYS;
    }
} // namespace syscall::vfs
