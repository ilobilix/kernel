// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import drivers.fs.tmpfs;
import magic_enum;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        enum mfd_flags : std::uint32_t
        {
            mfd_cloexec = 0x0001,
            mfd_allow_sealing = 0x0002,
            mfd_hugetlb = 0x0004,
            mfd_noexec_seal = 0x0008,
            mfd_exec = 0x0010
        };

        // excluding null
        constexpr std::size_t memfd_name_max = 249;
    } // namespace

    int memfd_create(const char __user *name, std::uint32_t flags)
    {
        if (!magic_enum::enum_flags_contains<mfd_flags>(flags))
            return -EINVAL;

        if (flags & mfd_hugetlb)
        {
            if (flags & mfd_allow_sealing)
                return -EINVAL;

            // TODO: check hugetlb_shm_group
            if (!sched::capable(sched::cap_t::ipc_lock))
                return -EPERM;

            // TODO
            return -EINVAL;
        }

        auto str = lib::user_string::get(name, memfd_name_max + 1);
        if (!str.has_value())
            return -EFAULT;

        constexpr auto mode = stat::s_ifreg | 0777;
        auto inode = fs::tmpfs::anon_inode(mode, (flags & mfd_allow_sealing) != 0);
        if (!inode)
            return -ENOMEM;

        // TODO
        // if (flags & mfd_noexec_seal)
        //     inode->seals |= seal_exec;

        const auto ret = create_anon_fd({
            .name = "memfd:" + *str,
            .ops = fs::tmpfs::ops_t::singleton(),
            .file_private_data = nullptr,
            .inode_private_data = nullptr,
            .st_mode = mode,
            .flags = o_rdwr | ((flags & mfd_cloexec) ? o_cloexec : 0),
            .skip_open = true,
            .inode = std::move(inode)
        });
        if (!ret)
            return -lib::map_error(ret.error());

        return ret->first;
    }

    int memfd_secret(std::uint32_t flags)
    {
        // TODO
        lib::unused(flags);
        return -ENOSYS;
    }
} // namespace syscall::vfs
