// Copyright (C) 2024-2025  ilobilo

export module system.vfs;

import system.memory.virt;
import lib;
import cppstd;

export namespace vfs
{
    inline constexpr std::size_t symloop_max = 40;
    inline constexpr std::size_t path_max = 4096;

    enum class error
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
        invalid_mount,
        invalid_symlink,
        invalid_device,
        invalid_type
    };

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
        o_sync = 04010000,

        changeable_status_flags = o_append | o_async | o_direct | o_noatime | o_nonblock,
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

    enum accchecks : int
    {
        f_ok = 0,
        x_ok = 1,
        w_ok = 2,
        r_ok = 4
    };

    // stat and s_* bits are defined in lib/types.cppm

    template<typename Type>
    using expect = std::expected<Type, error>;

    struct file;
    struct ops
    {
        virtual bool open(std::shared_ptr<file> self)
        {
            lib::unused(self);
            return true;
        }

        virtual bool close(std::shared_ptr<file> self)
        {
            lib::unused(self);
            return true;
        }

        virtual std::ssize_t read(std::shared_ptr<file> self, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual std::ssize_t write(std::shared_ptr<file> self, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual bool trunc(std::shared_ptr<file> self, std::size_t size) = 0;

        virtual int ioctl(std::shared_ptr<file> self, unsigned long request, lib::uptr_or_addr argp)
        {
            lib::unused(self, request, argp);
            return (errno = ENOTTY, -1);
        }

        virtual std::shared_ptr<vmm::object> map(std::shared_ptr<file> self, bool priv) = 0;

        virtual bool sync() = 0;

        virtual ~ops() = default;
    };

    struct dentry;
    struct mount;

    struct path
    {
        std::shared_ptr<mount> mnt;
        std::shared_ptr<dentry> dentry;
    };

    struct inode;
    struct filesystem
    {
        std::string name;

        struct instance
        {
            std::atomic<ino_t> next_inode = 1;
            dev_t dev_id;

            virtual auto create(std::shared_ptr<inode> &parent, std::string_view name, mode_t mode, std::shared_ptr<ops> ops = nullptr) -> expect<std::shared_ptr<inode>> = 0;

            virtual auto symlink(std::shared_ptr<inode> &parent, std::string_view name, lib::path target) -> expect<std::shared_ptr<inode>> = 0;
            virtual auto link(std::shared_ptr<inode> &parent, std::string_view name, std::shared_ptr<inode> target) -> expect<std::shared_ptr<inode>> = 0;
            virtual auto unlink(std::shared_ptr<inode> &inode) -> expect<void> = 0;

            virtual auto populate(std::shared_ptr<inode> &inode, std::string_view name = "") -> vfs::expect<std::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>> = 0;
            virtual bool sync() = 0;
            virtual bool unmount(std::shared_ptr<mount> mnt) = 0;

            virtual ~instance() = default;

            instance();
        };

        virtual auto mount(std::shared_ptr<dentry> src) const -> expect<std::shared_ptr<mount>> = 0;

        filesystem(std::string_view name) : name { name } { }
        virtual ~filesystem() = default;
    };

    struct mount
    {
        lib::locked_ptr<filesystem::instance, lib::mutex> fs;
        std::shared_ptr<dentry> root;
        std::optional<path> mounted_on;
    };

    struct inode
    {
        lib::mutex lock;
        std::shared_ptr<ops> op;
        stat stat;

        inode(std::shared_ptr<ops> op) : op { op } { }

        virtual ~inode() = default;
    };

    struct dentry : std::enable_shared_from_this<dentry>
    {
        static std::shared_ptr<dentry> root(bool absolute);

        std::string name;
        std::string symlinked_to;

        std::shared_ptr<inode> inode;

        std::weak_ptr<dentry> parent;
        lib::locker<
            lib::map::flat_hash<
                std::string_view,
                std::shared_ptr<dentry>
            >, lib::rwmutex
        > children;

        std::list<std::weak_ptr<mount>> child_mounts;
    };

    struct file : std::enable_shared_from_this<file>
    {
        inline std::shared_ptr<ops> &get_ops() const
        {
            lib::bug_on(!path.dentry || !path.dentry->inode);
            auto &inode = path.dentry->inode;
            lib::bug_on(!inode->op);
            return inode->op;
        }

        lib::mutex lock;
        path path;
        std::size_t offset;
        int flags;

        std::shared_ptr<void> private_data;

        bool open()
        {
            return get_ops()->open(shared_from_this());
        }

        bool close()
        {
            return get_ops()->close(shared_from_this());
        }

        std::ssize_t read(lib::maybe_uspan<std::byte> buffer)
        {
            std::unique_lock _ { lock };
            const auto ret = get_ops()->read(shared_from_this(), offset, buffer);
            if (ret > 0)
                offset += static_cast<std::size_t>(ret);
            return ret;
        }

        std::ssize_t write(lib::maybe_uspan<std::byte> buffer)
        {
            std::unique_lock _ { lock };
            const auto ret = get_ops()->write(shared_from_this(), offset, buffer);
            if (ret > 0)
                offset += static_cast<std::size_t>(ret);
            return ret;
        }

        std::ssize_t pread(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        {
            return get_ops()->read(shared_from_this(), offset, buffer);
        }

        std::ssize_t pwrite(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        {
            return get_ops()->write(shared_from_this(), offset, buffer);
        }

        bool trunc(std::size_t size)
        {
            return get_ops()->trunc(shared_from_this(), size);
        }

        int ioctl(unsigned long request, lib::uptr_or_addr argp)
        {
            return get_ops()->ioctl(shared_from_this(), request, argp);
        }

        std::shared_ptr<vmm::object> map(bool priv)
        {
            return get_ops()->map(shared_from_this(), priv);
        }

        static std::shared_ptr<file> create(const vfs::path &path, std::size_t offset, int flags)
        {
            auto file = std::make_shared<vfs::file>();
            file->path = path;
            file->offset = offset;
            file->flags = flags;
            return file;
        }
    };

    struct filedesc
    {
        std::shared_ptr<file> file { };
        std::atomic_bool closexec = false;

        static std::shared_ptr<filedesc> create(const path &path, int flags)
        {
            auto fd = std::make_shared<filedesc>();
            fd->file = vfs::file::create(path, 0, flags & ~creation_flags);
            fd->closexec = (flags & o_closexec) != 0;
            return fd;
        }
    };

    class fdtable
    {
        private:
        lib::locker<
            lib::map::flat_hash<
                int, std::shared_ptr<filedesc>
            >, lib::rwspinlock
        > fds;
        int next_fd = 0;

        public:
        bool close(int fd);
        std::shared_ptr<filedesc> get(int fd);
        int allocate_fd(std::shared_ptr<filedesc> desc, int fd, bool force);

        ~fdtable() = default;
    };

    struct resolve_res
    {
        path parent;
        path target;
    };

    path get_root(bool absolute);

    bool register_fs(std::unique_ptr<filesystem> fs);
    auto find_fs(std::string_view name) -> expect<std::reference_wrapper<std::unique_ptr<filesystem>>>;

    std::string pathname_from(path path);

    auto path_for(lib::path _path) -> expect<path>;
    auto resolve(std::optional<path> parent, lib::path path) -> expect<resolve_res>;
    auto reduce(path parent, path src, std::size_t symlink_depth = symloop_max) -> expect<path>;

    auto mount(lib::path source, lib::path target, std::string_view fstype, int flags) -> expect<void>;
    auto unmount(lib::path target) -> expect<void>;

    auto create(std::optional<path> parent, lib::path _path, mode_t mode, dev_t dev = 0) -> expect<path>;
    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> expect<path>;
    auto link(std::optional<path> parent, lib::path src, std::optional<path> tgtparent, lib::path target, bool follow_links = false) -> expect<path>;
    auto unlink(std::optional<path> parent, lib::path path) -> expect<void>;

    bool check_access(uid_t uid, gid_t gid, const stat &stat, int mode);

    auto stat(std::optional<path> parent, lib::path path) -> expect<stat>;
    bool populate(path parent, std::string_view name = "");

    lib::initgraph::stage *root_mounted_stage();
} // export namespace vfs