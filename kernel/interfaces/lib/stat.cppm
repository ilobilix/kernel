// Copyright (C) 2024-2026  ilobilo

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
        nlink_t st_nlink;
        mode_t st_mode;
        uid_t st_uid;
        gid_t st_gid;
        std::uint32_t pad0;
        dev_t st_rdev;
        off_t st_size;
        blksize_t st_blksize;
        blkcnt_t st_blocks;

        timespec st_atim;
        timespec st_mtim;
        timespec st_ctim;
        std::int64_t pad1[3];

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
    };

    struct kstat : stat
    {
        timespec st_btim;

        enum time : std::uint8_t
        {
            access = (1 << 0),
            modify = (1 << 1),
            status = (1 << 2),
            birth  = (1 << 3)
        };
        void update_time(std::uint8_t flags);
    };

    struct statx_timestamp
    {
        std::int64_t tv_sec;
        std::uint32_t tv_nsec;
        std::int32_t __reserved;
    };

    struct statx
    {
        std::uint32_t stx_mask;
        std::uint32_t stx_blksize;
        std::uint64_t stx_attributes;
        std::uint32_t stx_nlink;
        std::uint32_t stx_uid;
        std::uint32_t stx_gid;
        std::uint16_t stx_mode;
        std::uint16_t __spare0[1];
        std::uint64_t stx_ino;
        std::uint64_t stx_size;
        std::uint64_t stx_blocks;
        std::uint64_t stx_attributes_mask;
        statx_timestamp stx_atime;
        statx_timestamp stx_btime;
        statx_timestamp stx_ctime;
        statx_timestamp stx_mtime;
        std::uint32_t stx_rdev_major;
        std::uint32_t stx_rdev_minor;
        std::uint32_t stx_dev_major;
        std::uint32_t stx_dev_minor;
        std::uint64_t stx_mnt_id;
        std::uint32_t stx_dio_mem_align;
        std::uint32_t stx_dio_offset_align;
        std::uint64_t stx_subvol;
        std::uint32_t stx_atomic_write_unit_min;
        std::uint32_t stx_atomic_write_unit_max;
        std::uint32_t stx_atomic_write_segments_max;
        std::uint32_t stx_dio_read_offset_align;
        std::uint32_t stx_atomic_write_unit_max_opt;
        std::uint32_t __spare2[1];
        std::uint64_t __spare3[8];
    };

    struct kernel_fsid_t
    {
        std::int32_t val[2];
    };

    struct statfs
    {
        std::int64_t f_type;
        std::int64_t f_bsize;
        std::uint64_t f_blocks;
        std::uint64_t f_bfree;
        std::uint64_t f_bavail;
        std::uint64_t f_files;
        std::uint64_t f_ffree;
        kernel_fsid_t f_fsid;
        std::int64_t f_namelen;
        std::int64_t f_frsize;
        std::int64_t f_flags;
        std::int64_t f_spare[4];
    };
} // export
