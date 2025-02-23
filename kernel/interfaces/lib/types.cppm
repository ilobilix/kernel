// Copyright (C) 2024-2025  ilobilo

export module lib:types;

import magic_enum;
import std;

export namespace lib
{
    template<std::size_t N>
    struct bits2unit;

    template<>
    struct bits2unit<8> { using type = std::uint8_t; };

    template<>
    struct bits2unit<16> { using type = std::uint16_t; };

    template<>
    struct bits2unit<32> { using type = std::uint32_t; };

    template<>
    struct bits2unit<64> { using type = std::uint64_t; };

    template<std::size_t N>
    using bits2uint_t = bits2unit<N>::type;

    template<typename ...Funcs>
    struct overloaded : Funcs... { using Funcs::operator()...; };

    template<typename ...Funcs>
    overloaded(Funcs ...) -> overloaded<Funcs...>;
} // export namespace lib

export
{
    using dev_t = std::uint64_t;
    using ino_t = std::uint64_t;
    using mode_t = std::int32_t;
    using nlink_t = std::uint64_t;
    using uid_t = std::uint32_t;
    using gid_t = std::uint32_t;
    using dev_t = std::uint64_t;
    using off_t = std::int64_t;
    using blksize_t = std::int64_t;
    using blkcnt_t = std::int64_t;

    enum class at
    {
        fdcwd = -100,
        symlink_follow = 0x400,
        symlink_nofollow = 0x100,
        removedir = 0x200,
        no_automount = 0x800,
        empty_path = 0x1000,
        eaccess = 0x200
    };

    enum class fmode : mode_t
    {
        s_irwxu = 00700, // user rwx
        s_irusr = 00400, // user r
        s_iwusr = 00200, // user w
        s_ixusr = 00100, // user x
        s_irwxg = 00070, // group rwx
        s_irgrp = 00040, // group r
        s_iwgrp = 00020, // group w
        s_ixgrp = 00010, // group x
        s_irwxo = 00007, // others rwx
        s_iroth = 00004, // others r
        s_iwoth = 00002, // others w
        s_ixoth = 00001, // others x
        s_isuid = 04000, // set-user-id
        s_isgid = 02000, // set-group-id
        s_isvtx = 01000, // set-sticky
    };

    struct timespec
    {
        long tv_sec;
        long tv_nsec;
    };

    struct stat
    {
        enum type : mode_t
        {
            s_ifmt = 0170000,
            s_ifsock = 0140000,
            s_iflnk = 0120000,
            s_ifreg = 0100000,
            s_ifblk = 0060000,
            s_ifdir = 0040000,
            s_ifchr = 0020000,
            s_ififo = 0010000
        };

        dev_t st_dev;
        ino_t st_ino;
        mode_t st_mode;
        nlink_t st_nlink;
        uid_t st_uid;
        gid_t st_gid;
        dev_t st_rdev;
        off_t st_size;
        blksize_t st_blksize;
        blkcnt_t st_blocks;

        timespec st_atim;
        timespec st_mtim;
        timespec st_ctim;

        inline constexpr type type() const
        {
            return static_cast<enum type>(st_mode & static_cast<mode_t>(type::s_ifmt));
        }

        inline constexpr mode_t mode() const
        {
            return st_mode & ~static_cast<mode_t>(type::s_ifmt);
        }
    };

    using magic_enum::bitwise_operators::operator|;
} // export