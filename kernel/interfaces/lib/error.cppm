// Copyright (C) 2024-2026  ilobilo

export module lib:error;

import magic_enum;
import std;

import :errno;
import :panic;
import :bug_on;

export namespace lib
{
    enum class err
    {
        todo = ENOSYS,
        already_exists = EEXIST,
        does_not_exist = ENODATA,

        not_found = ENOENT,
        not_a_dir = ENOTDIR,
        not_a_block = ENOTBLK,

        symloop_max = ELOOP,

        binfmt_recursion = ELOOP,
        invalid_binfmt = ENOEXEC,

        target_is_a_dir = EISDIR,
        target_is_busy = EBUSY,

        dir_not_empty = ENOTEMPTY,

        different_filesystem = EXDEV,

        invalid_filesystem = ENODEV,
        no_such_device = ENODEV,
        invalid_device_or_address = ENXIO,

        invalid_path = EINVAL,
        invalid_symlink = EINVAL,
        invalid_flags = EINVAL,
        invalid_type = EINVAL,
        invalid_argument = EINVAL,
        invalid_pml_entry = EINVAL,
        invalid_length = EINVAL,
        addr_not_aligned = EINVAL,
        addr_out_of_bounds = EINVAL,
        buffer_too_small = EINVAL,

        path_too_long = ENAMETOOLONG,

        invalid_fd = EBADF,
        too_many_files = EMFILE,

        invalid_address = EFAULT,
        not_mapped = EFAULT,

        no_space_left = ENOSPC,

        try_again = EAGAIN,
        interrupted = EINTR,
        inappropriate_ioctl = ENOTTY,
        mapping_unsupported = ENODEV,
        io_error = EIO,

        out_of_memory = ENOMEM,

        not_permitted = EPERM,
        permission_denied = EACCES,

        not_supported = ENOTSUP,

        read_only_fs = EROFS,

        illegal_seek = ESPIPE,
        broken_pipe = EPIPE,

        bad_message = EBADMSG,
        corrupted_data = EBADMSG,

        timed_out = ETIMEDOUT,

        address_family_unsupported = EAFNOSUPPORT,
        protocol_unsupported = EPROTONOSUPPORT,
        socket_unsupported = ESOCKTNOSUPPORT,
        operation_unsupported = EOPNOTSUPP,
        wrong_protocol = EPROTOTYPE,
        not_a_socket = ENOTSOCK,
        already_connected = EISCONN,
        not_connected = ENOTCONN,
        connection_refused = ECONNREFUSED,
        message_too_long = EMSGSIZE,
        address_in_use = EADDRINUSE,
        already_in_progress = EALREADY,
        operation_in_progress = EINPROGRESS
    };

    template<typename Type>
    using expect = std::expected<Type, err>;

    constexpr errnos map_error(err err)
    {
        lib::bug_on(!magic_enum::enum_contains(err));
        return static_cast<errnos>(err);
    }

    std::string_view error_name(err e)
    {
        return magic_enum::enum_name(e);
    }
} // export namespace lib
