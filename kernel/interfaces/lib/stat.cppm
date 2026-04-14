// Copyright (C) 2024-2025  ilobilo

export module lib:stat;

import :types;
import :time;
import std;

export
{
    enum fmode : mode_t
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
        s_iread = s_irusr,
        s_iwrite = s_iwusr,
        s_iexec = s_ixusr
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

        static constexpr type type(mode_t mode)
        {
            return static_cast<enum type>(mode & static_cast<mode_t>(type::s_ifmt));
        }

        constexpr enum type type() const
        {
            return type(st_mode);
        }

        static constexpr mode_t mode(mode_t mode)
        {
            return mode & ~static_cast<mode_t>(type::s_ifmt);
        }

        constexpr mode_t mode() const
        {
            return mode(st_mode);
        }

        enum time : std::uint8_t { access = (1 << 0), modify = (1 << 1), status = (1 << 2) };
        void update_time(std::uint8_t flags);
    };
} // export