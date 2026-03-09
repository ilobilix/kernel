// Copyright (C) 2024-2026  ilobilo

export module system.vfs;

import system.scheduler.base;
import system.memory.virt;
import lib;
import std;

export namespace vfs
{
    inline constexpr std::size_t symloop_max = 40;
    inline constexpr std::size_t path_max = 4096;

    enum openflags : int
    {
        o_direct = 040000,
        o_largefile = 0100000,
        o_directory = 0200000,
        o_nofollow = 0400000,

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

    struct [[gnu::packed]] dirent64
    {
        std::uint64_t d_ino;
        std::int64_t d_off;
        std::uint16_t d_reclen;
        std::uint8_t d_type;
        char d_name[];
    };

    enum dts : std::uint8_t
    {
        dt_unknown = 0,
        dt_fifo = 1,
        dt_chr = 2,
        dt_dir = 4,
        dt_blk = 6,
        dt_reg = 8,
        dt_lnk = 10,
        dt_sock = 12,
        dt_wht = 14
    };

    inline constexpr dts stat_to_dt(enum stat::type type)
    {
        switch (type)
        {
            case stat::type::s_ififo:
                return dts::dt_fifo;
            case stat::type::s_ifchr:
                return dts::dt_chr;
            case stat::type::s_ifdir:
                return dts::dt_dir;
            case stat::type::s_ifblk:
                return dts::dt_blk;
            case stat::type::s_ifreg:
                return dts::dt_reg;
            case stat::type::s_iflnk:
                return dts::dt_lnk;
            case stat::type::s_ifsock:
                return dts::dt_sock;
            default:
                return dts::dt_unknown;
        }
    }

    enum pollevents : std::uint16_t
    {
        pollin   = 0x001, // there is data to read
        pollpri  = 0x002, // there is urgent data to read
        pollout  = 0x004, // writing now will not block
        pollerr  = 0x008, // error condition
        pollhup  = 0x010, // hung up
        pollnval = 0x020  // invalid request
    };

    struct poll_table
    {
        virtual void queue_wait(lib::wait_queue *wq) = 0;
        virtual ~poll_table() = default;
    };

    struct file;
    struct ops
    {
        virtual lib::expect<void> open(std::shared_ptr<file> file, int flags)
        {
            lib::unused(file, flags);
            return { };
        }

        virtual lib::expect<void> close(std::shared_ptr<file> file)
        {
            lib::unused(file);
            return { };
        }

        virtual lib::expect<std::size_t> read(std::shared_ptr<file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual lib::expect<std::size_t> write(std::shared_ptr<file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) = 0;
        virtual lib::expect<void> trunc(std::shared_ptr<file> file, std::size_t size) = 0;

        virtual lib::expect<std::size_t> getdents(std::shared_ptr<file> file, std::uint64_t &offset, lib::maybe_uspan<std::byte> buffer)
        {
            lib::unused(file, offset, buffer);
            return std::unexpected { lib::err::not_a_dir };
        }

        virtual lib::expect<std::uint16_t> poll(std::shared_ptr<file> file, poll_table *pt)
        {
            lib::unused(file, pt);
            return pollin | pollout;
        }

        virtual lib::expect<int> ioctl(std::shared_ptr<file> file, std::uint64_t request, lib::uptr_or_addr argp)
        {
            lib::unused(file, request, argp);
            return std::unexpected { lib::err::inappropriate_ioctl };
        }

        virtual lib::expect<std::shared_ptr<vmm::object>> map(std::shared_ptr<file> file, bool priv)
        {
            lib::unused(file, priv);
            return std::unexpected { lib::err::mapping_unsupported };
        }

        virtual lib::expect<void> sync() { return { }; }

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

            virtual auto create(std::shared_ptr<inode> &parent, std::string_view name, mode_t mode, dev_t rdev) -> lib::expect<std::shared_ptr<inode>> = 0;

            virtual auto symlink(std::shared_ptr<inode> &parent, std::string_view name, lib::path target) -> lib::expect<std::shared_ptr<inode>> = 0;
            virtual auto link(std::shared_ptr<inode> &parent, std::string_view name, std::shared_ptr<inode> target) -> lib::expect<std::shared_ptr<inode>> = 0;
            virtual auto unlink(std::shared_ptr<inode> &inode) -> lib::expect<void> = 0;

            virtual auto populate(std::shared_ptr<inode> &inode, std::string_view name = "") -> lib::expect<lib::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>> = 0;
            virtual bool sync() = 0;
            virtual bool unmount(std::shared_ptr<mount> mnt) = 0;

            virtual ~instance() = default;

            instance();
        };

        virtual auto mount(std::shared_ptr<dentry> src) const -> lib::expect<std::shared_ptr<mount>> = 0;

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
        stat stat;
    };

    struct dentry : std::enable_shared_from_this<dentry>
    {
        static std::shared_ptr<dentry> root(bool absolute);

        struct children
        {
            public:
            struct node
            {
                std::shared_ptr<dentry> dentry;
                std::size_t cookie;
            };

            private:
            lib::list<node> _child_list { };

            lib::map::flat_hash<
                std::string_view,
                lib::list<node>::iterator
            > _child_map { };

            lib::btree::map<
                std::size_t,
                lib::list<node>::iterator
            > _offset_map;

            std::size_t _next_cookie = 3;

            public:
            children() = default;

            void insert(std::shared_ptr<dentry> dentry)
            {
                lib::bug_on(_child_map.contains(dentry->name));
                _child_list.push_back({ dentry, _next_cookie++ });
                auto it = std::prev(_child_list.end());
                _child_map.insert({ dentry->name, it });
                _offset_map.insert({ it->cookie, it });
            }

            bool erase(std::string_view name)
            {
                const auto it = _child_map.find(name);
                if (it == _child_map.end())
                    return false;
                _offset_map.erase(it->second->cookie);
                _child_list.erase(it->second);
                _child_map.erase(it);
                return true;
            }

            std::shared_ptr<dentry> lookup(std::string_view name) const
            {
                const auto it = _child_map.find(name);
                if (it == _child_map.end())
                    return nullptr;
                return it->second->dentry;
            }

            lib::list<node>::const_iterator begin_at(std::size_t offset) const
            {
                const auto it = _offset_map.lower_bound(offset);
                if (it == _offset_map.end())
                    return _child_list.end();
                return it->second;
            }

            template<typename Type>
            auto begin(this Type &&self)
            {
                return std::forward<Type>(self)._child_list.begin();
            }

            template<typename Type>
            auto end(this Type &&self)
            {
                return std::forward<Type>(self)._child_list.end();
            }

            std::size_t size() const
            {
                const auto ret = _child_list.size();
                lib::bug_on(ret != _child_map.size() || ret != _offset_map.size());
                return ret;
            }

            bool empty() const
            {
                const auto ret = _child_list.empty();
                lib::bug_on(ret != _child_map.empty() || ret != _offset_map.empty());
                return ret;
            }
        };

        std::string name;
        std::string symlinked_to;

        std::shared_ptr<inode> inode;

        std::weak_ptr<dentry> parent;

        lib::locker<children, lib::rwmutex> children;

        lib::list<std::weak_ptr<mount>> child_mounts;
    };

    struct file : std::enable_shared_from_this<file>
    {
        auto get_ops() const -> lib::expect<std::shared_ptr<ops>>;

        lib::mutex lock;
        std::atomic<std::uint32_t> ref;
        path path;
        std::size_t offset;
        int flags;
        pid_t pid;

        std::shared_ptr<void> private_data;

        lib::expect<void> open(int flags)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->open(shared_from_this(), flags);
        }

        lib::expect<void> close()
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->close(shared_from_this());
        }

        lib::expect<std::size_t> read(lib::maybe_uspan<std::byte> buffer)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };

            std::unique_lock _ { lock };
            const auto ret = ops->get()->read(shared_from_this(), offset, buffer);
            if (ret.has_value())
                offset += *ret;
            return ret;
        }

        lib::expect<std::size_t> write(lib::maybe_uspan<std::byte> buffer)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };

            std::unique_lock _ { lock };
            const auto ret = ops->get()->write(shared_from_this(), offset, buffer);
            if (ret.has_value())
                offset += *ret;
            return ret;
        }

        lib::expect<std::size_t> pread(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->read(shared_from_this(), offset, buffer);
        }

        lib::expect<std::size_t> pwrite(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->write(shared_from_this(), offset, buffer);
        }

        lib::expect<void> trunc(std::size_t size)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->trunc(shared_from_this(), size);
        }

        lib::expect<std::size_t> getdents(lib::maybe_uspan<std::byte> buffer);

        lib::expect<std::uint16_t> poll(poll_table *pt)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->poll(shared_from_this(), pt);
        }

        lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->ioctl(shared_from_this(), request, argp);
        }

        lib::expect<std::shared_ptr<vmm::object>> map(bool priv)
        {
            const auto ops = get_ops();
            if (!ops.has_value())
                return std::unexpected { ops.error() };
            return ops->get()->map(shared_from_this(), priv);
        }

        static std::shared_ptr<file> create(const vfs::path &path, std::size_t offset, int flags, pid_t pid)
        {
            auto file = std::make_shared<vfs::file>();
            file->ref = 1;
            file->path = path;
            file->offset = offset;
            file->flags = flags;
            file->pid = pid;
            return file;
        }
    };

    struct filedesc
    {
        std::shared_ptr<file> file { };
        std::atomic_bool closexec = false;

        static std::shared_ptr<filedesc> create(const path &path, int flags, pid_t pid)
        {
            auto fd = std::make_shared<filedesc>();
            fd->file = vfs::file::create(path, 0, flags & ~creation_flags, pid);
            fd->closexec = (flags & o_closexec) != 0;
            return fd;
        }
    };

    struct resolve_res
    {
        path parent;
        path target;
    };

    path get_root(bool absolute);

    bool register_fs(std::unique_ptr<filesystem> fs);
    auto find_fs(std::string_view name) -> lib::expect<std::reference_wrapper<std::unique_ptr<filesystem>>>;

    std::string pathname_from(path path);

    auto path_for(lib::path _path) -> lib::expect<path>;
    auto resolve(std::optional<path> parent, lib::path path) -> lib::expect<resolve_res>;
    auto reduce(path parent, path src, std::size_t symlink_depth = symloop_max) -> lib::expect<path>;

    auto mount(lib::path source, lib::path target, std::string_view fstype, int flags) -> lib::expect<void>;
    auto unmount(lib::path target) -> lib::expect<void>;

    auto create(std::optional<path> parent, lib::path _path, mode_t mode, dev_t rdev = 0) -> lib::expect<path>;
    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> lib::expect<path>;
    auto link(std::optional<path> parent, lib::path src, std::optional<path> tgtparent, lib::path target, bool follow_links = false) -> lib::expect<path>;
    auto unlink(std::optional<path> parent, lib::path path) -> lib::expect<void>;

    bool check_access(uid_t uid, gid_t gid, const std::span<const gid_t> &supgids, const stat &stat, int mode);

    bool populate(path parent, std::string_view name = "");

    lib::initgraph::stage *root_mounted_stage();
} // export namespace vfs
