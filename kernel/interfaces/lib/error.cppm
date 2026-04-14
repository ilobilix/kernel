// Copyright (C) 2024-2026  ilobilo

export module lib:error;

import :errno;
import :panic;
import magic_enum;
import std;

export namespace lib
{
    enum class err
    {
        todo,
        already_exists,

        not_found,
        not_a_dir,
        not_a_block,

        symloop_max,

        target_is_a_dir,
        target_is_busy,

        dir_not_empty,

        different_filesystem,

        invalid_filesystem,
        no_such_device,
        invalid_device_or_address,
        invalid_mount,
        invalid_symlink,
        invalid_flags,
        invalid_type,
        invalid_entry,
        invalid_address,
        invalid_fd,

        buffer_too_small,

        no_space_left,
        no_readers,

        try_again,
        interrupted,
        inappropriate_ioctl,
        mapping_unsupported,
        io_error,

        addr_not_aligned,
        addr_in_use,
        not_mapped
    };

    template<typename Type>
    using expect = std::expected<Type, err>;

    constexpr errnos map_error(err err)
    {
        switch (err)
        {
            case err::todo:
                return ENOSYS;
            case err::already_exists:
                return EEXIST;
            case err::not_found:
                return ENOENT;
            case err::not_a_dir:
                return ENOTDIR;
            case err::not_a_block:
                return ENOTBLK;
            case err::symloop_max:
                return ELOOP;
            case err::target_is_a_dir:
                return EISDIR;
            case err::target_is_busy:
                return EBUSY;
            case err::dir_not_empty:
                return ENOTEMPTY;
            case err::different_filesystem:
                return EXDEV;
            case err::invalid_filesystem:
            case err::no_such_device:
                return ENODEV;
            case err::invalid_device_or_address:
                return ENXIO;
            case err::invalid_mount:
            case err::invalid_symlink:
            case err::invalid_flags:
            case err::buffer_too_small:
                return EINVAL;
            // case err::invalid_entry:
            //     return ;
            case err::invalid_address:
                return EFAULT;
            case err::invalid_fd:
                return EBADF;
            case err::no_space_left:
                return ENOSPC;
            case err::no_readers:
                return EPIPE;
            case err::try_again:
                return EAGAIN;
            case err::interrupted:
                return EINTR;
            case err::inappropriate_ioctl:
                return ENOTTY;
            case err::mapping_unsupported:
                return ENODEV;
            case err::io_error:
                return EIO;
            // case err::addr_not_aligned:
            //     return ;
            // case err::addr_in_use:
            //     return ;
            // case err::not_mapped:
            //     return ;
            default:
                lib::panic("unhandled vfs err: {}", magic_enum::enum_name(err));
        }
        std::unreachable();
    }

} // export namespace lib
