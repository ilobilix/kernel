// Copyright (C) 2024-2026  ilobilo

export module system.vfs;

export import system.vfs.caps;

import system.sched.thread_base;
import system.sched.wait_queue;
import system.sched.mutex;
import system.sched.cred;
import system.memory.virt;
import lib;
import std;

export namespace vfs
{
    constexpr std::size_t symloop_max = 40;
    constexpr std::size_t path_max = 4096;

    constexpr std::size_t xattr_size_max = 65536;
    constexpr std::size_t xattr_list_max = 65536;
    constexpr std::size_t xattr_name_max = 255;

    constexpr int xattr_create = 1;
    constexpr int xattr_replace = 2;

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
        o_cloexec = 02000000,
        o_tmpfile = 020000000 | o_directory,

        creation_flags = o_creat | o_excl | o_noctty | o_trunc | o_directory | o_nofollow | o_cloexec | o_tmpfile,

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
        at_empty_path = 0x1000,
        at_statx_force_sync = 0x2000,
        at_statx_sync_type = 0x6000,
        at_statx_dont_sync = 0x4000
    };

    enum accchecks : int
    {
        f_ok = 0,
        x_ok = 1,
        w_ok = 2,
        r_ok = 4
    };

    enum mountflags : std::uint64_t
    {
        ms_rdonly      = 1ul << 0,
        ms_nosuid      = 1ul << 1,
        ms_nodev       = 1ul << 2,
        ms_noexec      = 1ul << 3,
        ms_synchronous = 1ul << 4,
        ms_remount     = 1ul << 5,
        ms_mandlock    = 1ul << 6,
        ms_dirsync     = 1ul << 7,
        ms_nosymfollow = 1ul << 8,
        ms_noatime     = 1ul << 10,
        ms_nodiratime  = 1ul << 11,
        ms_bind        = 1ul << 12,
        ms_move        = 1ul << 13,
        ms_rec         = 1ul << 14,
        ms_silent      = 1ul << 15,
        ms_posixacl    = 1ul << 16,
        ms_unbindable  = 1ul << 17,
        ms_private     = 1ul << 18,
        ms_slave       = 1ul << 19,
        ms_shared      = 1ul << 20,
        ms_relatime    = 1ul << 21,
        ms_kernmount   = 1ul << 22,
        ms_i_version   = 1ul << 23,
        ms_strictatime = 1ul << 24,
        ms_lazytime    = 1ul << 25,

        ms_supported = ms_rdonly | ms_nosuid | ms_nodev | ms_noexec |
                       ms_synchronous | ms_remount | ms_dirsync |
                       ms_nosymfollow | ms_noatime | ms_nodiratime |
                       ms_silent | ms_relatime | ms_strictatime |
                       ms_lazytime
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
        pollin     = 0x001,  // there is data to read
        pollpri    = 0x002,  // there is urgent data to read
        pollout    = 0x004,  // writing now will not block
        pollerr    = 0x008,  // error condition
        pollhup    = 0x010,  // hung up
        pollnval   = 0x020,  // invalid request
        pollrdnorm = 0x040,  // normal data may be read
        pollrdband = 0x080,  // priority data may be read
        pollwrnorm = 0x100,  // writing now will not block
        pollwrband = 0x200,  // priority data may be written
        pollmsg    = 0x400,  // linux extensions
        pollremove = 0x1000,
        pollrdhup  = 0x2000
    };

    struct poll_table_t
    {
        virtual void add(sched::wait_queue_t &wq) = 0;
        virtual ~poll_table_t() = default;
    };

    struct file_t;
    struct inode_t;
    struct ops_t
    {
        virtual bool truncable() const { return false; }
        virtual bool seekable() const { return true; }

        virtual lib::expect<void> open(std::shared_ptr<file_t> file, int flags, pid_t pid)
        {
            lib::unused(file, flags, pid);
            return { };
        }

        virtual lib::expect<void> close(file_t &file)
        {
            lib::unused(file);
            return { };
        }

        virtual lib::expect<std::size_t> read(
            std::shared_ptr<file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) = 0;
        virtual lib::expect<std::size_t> write(
            std::shared_ptr<file_t> file, std::uint64_t offset,
            lib::maybe_uspan<std::byte> buffer
        ) = 0;

        virtual lib::expect<void> trunc(std::shared_ptr<file_t> file, std::size_t size)
        {
            lib::unused(file, size);
            return std::unexpected { lib::err::invalid_argument };
        }

        virtual lib::expect<void> getattr(std::shared_ptr<inode_t> inode)
        {
            lib::unused(inode);
            return { };
        }

        virtual lib::expect<std::uint16_t> poll(std::shared_ptr<file_t> file, poll_table_t *pt)
        {
            lib::unused(file, pt);
            return pollin | pollout;
        }

        virtual lib::expect<int> ioctl(
            std::shared_ptr<file_t> file, std::uint64_t request,
            lib::uptr_or_addr argp
        )
        {
            lib::unused(file, request, argp);
            return std::unexpected { lib::err::inappropriate_ioctl };
        }

        virtual lib::expect<vmm::object::ptr> map(std::shared_ptr<file_t> file);

        virtual lib::expect<void> sync() { return { }; }

        virtual ~ops_t() = default;
    };

    struct dentry_t;
    struct mount_t;

    struct path_t
    {
        std::shared_ptr<mount_t> mnt;
        std::shared_ptr<dentry_t> dentry;
    };

    struct inode_t;
    struct dir_entry
    {
        std::string name;
        std::shared_ptr<inode_t> inode;
        std::size_t cookie;
    };

    struct filesystem_t
    {
        const std::string name;
        const std::uint32_t magic;
        const bool requires_dev;

        struct instance_t
        {
            filesystem_t *fs = nullptr;
            std::atomic<ino_t> next_inode = 1;
            dev_t dev_id;

            virtual auto create(
                std::shared_ptr<inode_t> &parent, std::string_view name,
                mode_t mode, dev_t rdev, std::optional<std::shared_ptr<ops_t>> ops
            ) -> lib::expect<std::shared_ptr<inode_t>> = 0;

            virtual auto symlink(
                std::shared_ptr<inode_t> &parent,
                std::string_view name, lib::path target
            ) -> lib::expect<std::shared_ptr<inode_t>> = 0;

            virtual auto link(
                std::shared_ptr<inode_t> &parent,
                std::string_view name, std::shared_ptr<inode_t> target
            ) -> lib::expect<std::shared_ptr<inode_t>> = 0;

            virtual auto unlink(std::shared_ptr<inode_t> &inode) -> lib::expect<void> = 0;

            virtual auto rename(
                std::shared_ptr<inode_t> &old_parent, std::string_view old_name,
                std::shared_ptr<inode_t> &new_parent, std::string_view new_name,
                std::shared_ptr<inode_t> replaced
            ) -> lib::expect<void> = 0;

            virtual auto readdir(std::shared_ptr<dentry_t> dir, std::size_t cookie)
                -> lib::expect<lib::list<dir_entry>> = 0;

            virtual auto lookup(std::shared_ptr<dentry_t> dir,std::string_view name)
                -> lib::expect<dir_entry>;

            virtual auto readlink(std::shared_ptr<dentry_t> dentry) -> lib::expect<lib::path>;

            virtual bool revalidate(std::shared_ptr<dentry_t> dentry)
            {
                lib::unused(dentry);
                return true;
            }

            virtual bool permission(
                std::shared_ptr<dentry_t> dentry,
                const std::shared_ptr<sched::cred_t> &cred,
                std::uint32_t mode
            );

            virtual std::string mount_options() const { return { }; }

            virtual void statfs(struct ::statfs &out);

            virtual auto write_inode(std::shared_ptr<inode_t> &inode) -> lib::expect<void> = 0;
            virtual auto dirty_inode(std::shared_ptr<inode_t> &inode) -> lib::expect<void> = 0;

            virtual auto getxattr(std::shared_ptr<inode_t> &inode, std::string_view name)
                -> lib::expect<lib::membuffer>
            {
                lib::unused(inode, name);
                return std::unexpected { lib::err::not_supported };
            }

            virtual auto setxattr(
                std::shared_ptr<inode_t> &inode, std::string_view name,
                lib::maybe_uspan<std::byte> data, int flags
            ) -> lib::expect<void>
            {
                lib::unused(inode, name, data, flags);
                return std::unexpected { lib::err::not_supported };
            }

            virtual auto remxattr(std::shared_ptr<inode_t> &inode, std::string_view name)
                -> lib::expect<void>
            {
                lib::unused(inode, name);
                return std::unexpected { lib::err::not_supported };
            }

            virtual auto listxattrs(std::shared_ptr<inode_t> &inode)
                -> lib::expect<std::vector<std::string>>
            {
                lib::unused(inode);
                return std::unexpected { lib::err::not_supported };
            }

            virtual bool sync() = 0;
            virtual bool unmount(std::shared_ptr<mount_t> mnt) = 0;

            virtual ~instance_t() = default;

            instance_t();
        };

        virtual auto mount(
            std::shared_ptr<dentry_t> src,
            std::optional<lib::maybe_uspan<const std::byte>> data
        ) const -> lib::expect<std::shared_ptr<mount_t>> = 0;

        filesystem_t(std::string_view name, std::uint32_t magic = 0, bool requires_dev = false)
            : name { name }, magic { magic }, requires_dev { requires_dev } { }

        virtual ~filesystem_t() = default;
    };

    struct mount_t
    {
        lib::locked_ptr<filesystem_t::instance_t, sched::mutex> fs;
        std::shared_ptr<dentry_t> root;
        std::optional<path_t> mounted_on;

        std::size_t id;
        std::size_t parent_id;
        std::uint64_t flags = 0;

        std::string fstype;
        std::string source;

        mount_t(lib::locked_ptr<filesystem_t::instance_t, sched::mutex> fs, std::shared_ptr<dentry_t> root)
            : fs { std::move(fs) }, root { std::move(root) } { }
    };

    struct inode_t
    {
        sched::mutex lock;
        std::shared_ptr<ops_t> ops;
        kstat stat;
        bool dirty = false;

        lib::map::flat_hash<
            std::string,
            lib::membuffer
        > xattrs;

        std::shared_ptr<void> private_data;
        vmm::object::ptr mapping;

        std::shared_ptr<struct ops_t> get_ops();

        void invalidate_pcache(std::uint64_t offset, std::size_t length);
        void trunc_pcache(std::size_t size);
        void orphan_pcache();

        inode_t(std::shared_ptr<struct ops_t> ops) : ops { ops } { }
    };

    struct dentry_t : std::enable_shared_from_this<dentry_t>
    {
        static std::shared_ptr<dentry_t> root(bool absolute);
        static std::shared_ptr<dentry_t> create();

        struct children
        {
            public:
            struct node
            {
                std::shared_ptr<dentry_t> dentry;
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

            void insert(std::shared_ptr<dentry_t> dentry)
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

            std::shared_ptr<dentry_t> lookup(std::string_view name) const
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
        lib::path symlinked_to;

        std::shared_ptr<inode_t> inode;

        std::weak_ptr<dentry_t> parent;
        lib::locker<children, sched::mutex> children;

        lib::locker<lib::list<std::weak_ptr<mount_t>>, sched::mutex> child_mounts;
    };

    struct file_t : std::enable_shared_from_this<file_t>
    {
        sched::mutex lock;
        std::shared_ptr<ops_t> ops;
        bool opened;
        path_t path;
        std::size_t offset;
        int flags;

        std::shared_ptr<void> private_data;

        ~file_t()
        {
            if (!opened)
                return;

            if (!ops)
            {
                lib::error(
                    "failed to close file: {}",
                    lib::error_name(lib::err::invalid_device_or_address)
                );
                return;
            }

            if (const auto ret = ops->close(*this); !ret)
                lib::error("failed to close file: {}", lib::error_name(ret.error()));
        }

        bool truncable() const
        {
            if (!ops)
                return false;
            return ops->truncable();
        }

        lib::expect<void> open(int flags, pid_t pid)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            auto ret = ops->open(shared_from_this(), flags, pid);
            if (ret.has_value())
                opened = true;
            return ret;
        }

        lib::expect<std::size_t> read(lib::maybe_uspan<std::byte> buffer)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            if (!ops->seekable())
                return ops->read(shared_from_this(), offset, buffer);
            const std::unique_lock _ { lock };
            const auto ret = ops->read(shared_from_this(), offset, buffer);
            if (ret.has_value())
                offset += *ret;
            return ret;
        }

        lib::expect<std::size_t> write(lib::maybe_uspan<std::byte> buffer)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            if (!ops->seekable())
                return ops->write(shared_from_this(), offset, buffer);
            const std::unique_lock _ { lock };
            const auto ret = ops->write(shared_from_this(), offset, buffer);
            if (ret.has_value())
            {
                if (path.dentry && path.dentry->inode)
                    path.dentry->inode->invalidate_pcache(offset, *ret);
                offset += *ret;
            }
            return ret;
        }

        lib::expect<std::size_t> pread(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            return ops->read(shared_from_this(), offset, buffer);
        }

        lib::expect<std::size_t> pwrite(std::uint64_t offset, lib::maybe_uspan<std::byte> buffer)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            const auto ret = ops->write(shared_from_this(), offset, buffer);
            if (ret.has_value() && path.dentry && path.dentry->inode)
                path.dentry->inode->invalidate_pcache(offset, *ret);
            return ret;
        }

        lib::expect<void> trunc(std::size_t size)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            const auto ret = ops->trunc(shared_from_this(), size);
            if (ret.has_value() && path.dentry && path.dentry->inode)
                path.dentry->inode->trunc_pcache(size);
            return ret;
        }

        lib::expect<std::size_t> getdents(lib::maybe_uspan<std::byte> buffer);

        lib::expect<std::uint16_t> poll(poll_table_t *pt)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            return ops->poll(shared_from_this(), pt);
        }

        lib::expect<int> ioctl(std::uint64_t request, lib::uptr_or_addr argp)
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            return ops->ioctl(shared_from_this(), request, argp);
        }

        lib::expect<vmm::object::ptr> map()
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };
            return ops->map(shared_from_this());
        }

        lib::expect<void> sync()
        {
            if (!ops)
                return std::unexpected { lib::err::invalid_device_or_address };

            if (auto ret = ops->sync(); !ret.has_value())
                return ret;

            auto &inode = path.dentry->inode;
            const std::unique_lock _ { inode->lock };
            if (inode->dirty)
            {
                if (auto ret = path.mnt->fs.lock()->write_inode(inode); !ret)
                    return ret;
            }
            return { };
        }

        static std::shared_ptr<file_t> create(const vfs::path_t &path, std::size_t offset, int flags)
        {
            auto file = std::make_shared<vfs::file_t>();
            file->opened = false;
            file->path = path;
            file->offset = offset;
            file->flags = flags;
            if (path.dentry && path.dentry->inode)
                file->ops = path.dentry->inode->get_ops();
            return file;
        }
    };

    struct filedesc
    {
        std::shared_ptr<file_t> file { };
        std::atomic_bool closexec = false;

        static std::shared_ptr<filedesc> create(const path_t &path, int flags)
        {
            auto fd = std::make_shared<filedesc>();
            fd->file = vfs::file_t::create(path, 0, flags & ~creation_flags);
            fd->closexec = (flags & o_cloexec) != 0;
            return fd;
        }
    };

    struct fdtable
    {
        lib::locker<
            lib::map::flat_hash<
                int,
                std::shared_ptr<vfs::filedesc>
            >, lib::rwspinlock
        > fds;
        int next_fd = 0;

        std::shared_ptr<vfs::filedesc> get(int fd);
        bool close(int fd);

        lib::expect<int> alloc(std::shared_ptr<vfs::filedesc> desc, int fd, bool force, rlim_t max_fd = rlim_inf);
        lib::expect<int> dup(int oldfd, int newfd, bool closexec, bool force, rlim_t max_fd = rlim_inf);

        void close_on_exec();

        std::shared_ptr<fdtable> clone();

        fdtable() = default;
        fdtable(fdtable &other);
    };

    struct resolve_res
    {
        path_t parent;
        path_t target;
    };

    path_t get_root(bool absolute);
    std::shared_ptr<mount_t> get_mount(std::size_t id);

    bool register_fs(filesystem_t &fs);
    bool unregister_fs(filesystem_t &fs);
    filesystem_t *find_fs(std::string_view name);

    std::string pathname_from(path_t path);

    auto path_for(lib::path _path) -> lib::expect<path_t>;
    auto resolve(std::optional<path_t> parent, lib::path path, bool automount = true)
        -> lib::expect<resolve_res>;
    auto reduce(
        path_t parent, path_t src, bool automount = true,
        std::size_t symlink_depth = symloop_max
    ) -> lib::expect<path_t>;

    auto mount(
        lib::path source_path, lib::path target_path,
        std::string_view fstype, std::uint64_t flags,
        std::optional<lib::maybe_uspan<const std::byte>> data = std::nullopt
    ) -> lib::expect<void>;
    auto unmount(lib::path target) -> lib::expect<void>;

    bool check_access(
        const path_t &target,
        const std::shared_ptr<sched::cred_t> &cred,
        std::uint32_t mode
    );

    bool check_access(const path_t &target, std::uint32_t mode);

    auto create(
        std::optional<path_t> parent, lib::path _path,
        mode_t mode, dev_t rdev = 0, std::shared_ptr<ops_t> ops = nullptr
    ) -> lib::expect<path_t>;
    auto symlink(std::optional<path_t> parent, lib::path src, lib::path target) -> lib::expect<path_t>;
    auto link(
        std::optional<path_t> parent, lib::path src,
        std::optional<path_t> tgtparent, lib::path target,
        bool follow_links = false
    ) -> lib::expect<path_t>;
    auto unlink(std::optional<path_t> parent, lib::path path) -> lib::expect<void>;

    auto rename(
        std::optional<path_t> old_parent, lib::path old_path,
        std::optional<path_t> new_parent, lib::path new_path
    ) -> lib::expect<void>;

    // called with path.dentry->inode->lock acquired
    auto dirty_inode(const path_t &path) -> lib::expect<void>;

    auto getxattr(const path_t &target, std::string_view name) -> lib::expect<lib::membuffer>;
    auto setxattr(
        const path_t &target, std::string_view name,
        lib::maybe_uspan<std::byte> data, int flags
    ) -> lib::expect<void>;
    auto remxattr(const path_t &target, std::string_view name) -> lib::expect<void>;

    auto listxattrs(const path_t &target) -> lib::expect<std::vector<std::string>>;
    auto lenxattrs(const path_t &target) -> lib::expect<std::size_t>;

    struct anon_fd_args
    {
        std::string_view name;
        std::shared_ptr<ops_t> ops;
        std::shared_ptr<void> file_private_data;
        std::shared_ptr<void> inode_private_data;
        mode_t st_mode;
        int flags;
        bool skip_open;
        std::shared_ptr<vfs::inode_t> inode;
    };

    auto create_anon_fd(const anon_fd_args &args)
        -> lib::expect<std::pair<int, std::shared_ptr<filedesc>>>;

    lib::initgraph::stage *root_mounted_stage();
} // export namespace vfs
