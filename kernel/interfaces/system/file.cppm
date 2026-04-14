// Copyright (C) 2024-2025  ilobilo

export module system.vfs.file;

import system.vfs;
import lib;
import cppstd;

export namespace vfs
{
    enum openflags : int
    {
#if defined(__x86_64__)
        o_direct = 040000,
        o_largefile = 0100000,
        o_directory = 0200000,
        o_nofollow = 0400000,
#elif defined(__aarch64__)
        o_directory = 040000,
        o_nofollow = 0100000,
        o_direct = 0200000,
        o_largefile = 0400000,
#else
#  error "unsupported architecture"
#endif

        o_path = 010000000,

        // access modes
        o_accmode = 03 | o_path,
        o_rdonly = 00,
        o_wronly = 01,
        o_rdwr = 02,

        // creation flags
        o_creat = 0100,
        o_excl = 0200,
        o_noctty = 0400,
        o_trunc = 01000,
        // o_directory
        // o_nofollow
        o_closexec = 02000000,
        o_tmpfile = 020000000 | o_directory,

        creation_flags = o_creat | o_excl | o_noctty | o_trunc | o_directory | o_nofollow | o_closexec | o_tmpfile,

        // status flags
        o_append = 02000,
        o_async = 020000,
        // o_direct
        o_dsync = 010000,
        // o_largefile
        o_noatime = 01000000,
        o_nonblock = 04000,
        o_ndelay = o_nonblock,
        o_sync = 04010000
    };

    inline constexpr bool is_read(int flags)
    {
        return (flags & o_accmode) == o_rdonly || (flags & o_accmode) == o_rdwr;
    }

    inline constexpr bool is_write(int flags)
    {
        return (flags & o_accmode) == o_wronly || (flags & o_accmode) == o_rdwr;
    }

    enum seekwhence : int
    {
        seek_set = 0,
        seek_cur = 1,
        seek_end = 2
    };

    enum atflags : int
    {
        at_fdcwd = -100,
        at_symlink_nofollow = 0x100,
        at_removedir = 0x200,
        at_symlink_follow = 0x400,
        at_eaccess = 0x200,
        at_no_automount = 0x800,
        at_empty_path = 0x1000
    };

    // stat and s_* bits are defined in lib/types.cppm

    struct file
    {
        path path;
        std::size_t offset;
        int flags;

        std::ssize_t read(std::span<std::byte> buffer)
        {
            lib::bug_on(!path.dentry || !path.dentry->inode);
            const auto ret = path.dentry->inode->read(offset, buffer);
            if (ret > 0)
                offset += static_cast<std::size_t>(ret);
            return ret;
        }

        std::ssize_t write(std::span<std::byte> buffer)
        {
            lib::bug_on(!path.dentry || !path.dentry->inode);
            const auto ret = path.dentry->inode->write(offset, buffer);
            if (ret > 0)
                offset += static_cast<std::size_t>(ret);
            return ret;
        }

        std::ssize_t pread(std::uint64_t offset, std::span<std::byte> buffer)
        {
            lib::bug_on(!path.dentry || !path.dentry->inode);
            return path.dentry->inode->read(offset, buffer);
        }

        std::ssize_t pwrite(std::uint64_t offset, std::span<std::byte> buffer)
        {
            lib::bug_on(!path.dentry || !path.dentry->inode);
            return path.dentry->inode->write(offset, buffer);
        }

        bool trunc(std::size_t size)
        {
            lib::bug_on(!path.dentry || !path.dentry->inode);
            return path.dentry->inode->trunc(size);
        }
    };

    struct filedesc
    {
        std::shared_ptr<file> file;
        std::atomic_bool closexec;

        static std::shared_ptr<filedesc> create(const path &path, int flags)
        {
            auto fd = std::make_shared<filedesc>();
            fd->file = std::make_shared<vfs::file>(path, 0, flags & ~creation_flags);
            fd->closexec = (flags & o_closexec) != 0;
            return fd;
        }
    };

    class fdtable
    {
        private:
        lib::map::flat_hash<std::size_t, std::shared_ptr<filedesc>> fds;
        std::size_t next_fd = 0;

        public:
        inline bool close(std::size_t fd)
        {
            return fds.erase(fd);
        }

        inline std::shared_ptr<filedesc> get(std::size_t fd)
        {
            auto it = fds.find(fd);
            if (it == fds.end())
                return nullptr;
            return it->second;
        }

        std::optional<std::size_t> allocate_fd(std::shared_ptr<filedesc> desc, std::size_t fd, bool force)
        {
            if (fds.contains(fd))
            {
                if (!force)
                {
                    fd = next_fd++;
                    while (fds.contains(fd))
                        fd++;
                }
                else if (!close(fd))
                    return std::nullopt;
            }

            fds[fd] = desc;
            return fd;
        }

        ~fdtable() = default;
    };
} // export namespace vfs