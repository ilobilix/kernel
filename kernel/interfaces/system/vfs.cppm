// Copyright (C) 2024-2025  ilobilo

export module system.vfs;

import lib;
import cppstd;

export namespace vfs
{
    enum class error
    {
        todo,
        already_exists,
        not_found,
        not_a_dir,
        not_a_block,
        symloop_max,
        invalid_filesystem,
        invalid_mount,
        invalid_symlink
    };

    template<typename Type>
    using expect = std::expected<Type, error>;

    struct inode;
    struct ops
    {
        virtual std::ssize_t read(std::shared_ptr<inode> self, std::uint64_t offset, std::span<std::byte> buffer) = 0;
        virtual std::ssize_t write(std::shared_ptr<inode> self, std::uint64_t offset, std::span<std::byte> buffer) = 0;

        virtual std::uintptr_t mmap(std::shared_ptr<inode> self, std::uintptr_t page, int flags) = 0;
        virtual bool munmap(std::shared_ptr<inode> self, std::uintptr_t page) = 0;

        virtual bool sync() = 0;

        virtual ~ops() = default;
    };

    struct node;
    struct mount;
    struct filesystem
    {
        std::string name;

        struct instance
        {
            lib::mutex lock;

            std::shared_ptr<filesystem> fs;

            virtual auto create(std::shared_ptr<inode> &parent, std::string_view name, mode_t mode, std::shared_ptr<ops> ops = nullptr) -> expect<std::shared_ptr<inode>> = 0;

            virtual auto symlink(std::shared_ptr<inode> &parent, std::string_view name, lib::path target) -> expect<std::shared_ptr<inode>> = 0;
            virtual auto link(std::shared_ptr<inode> &parent, std::string_view name, std::shared_ptr<inode> target) -> expect<std::shared_ptr<inode>> = 0;
            virtual auto unlink(std::shared_ptr<inode> &node) -> expect<void> = 0;

            virtual auto populate(std::shared_ptr<vfs::inode> &node, std::string_view name = "") -> vfs::expect<std::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>> = 0;
            virtual bool sync() = 0;
            virtual bool unmount() = 0;

            virtual ~instance() = default;
        };

        virtual auto mount(std::shared_ptr<node> &) const -> std::pair<std::shared_ptr<instance>, std::shared_ptr<node>> = 0;

        filesystem(std::string_view name) : name { name } { }
        virtual ~filesystem() = default;
    };

    struct mount
    {
        std::shared_ptr<filesystem::instance> fs;
        std::weak_ptr<node> root;
        std::weak_ptr<node> mounted_on;

        mount(std::shared_ptr<filesystem::instance> fs, std::weak_ptr<node> root, std::weak_ptr<node> mounted_on)
            : fs { fs }, root { root }, mounted_on { mounted_on } { }
    };

    struct inode : std::enable_shared_from_this<inode>
    {
        std::ssize_t read(std::size_t offset, std::span<std::byte> buffer)
        {
            lib::ensure(op != nullptr);
            return op->read(shared_from_this(), offset, buffer);
        }

        std::ssize_t write(std::size_t offset, std::span<std::byte> buffer)
        {
            lib::ensure(op != nullptr);
            return op->write(shared_from_this(), offset, buffer);
        }

        std::uintptr_t mmap(std::uintptr_t page, int flags)
        {
            lib::ensure(op != nullptr);
            return op->mmap(shared_from_this(), page, flags);
        }

        bool munmap(std::uintptr_t page)
        {
            lib::ensure(op != nullptr);
            return op->munmap(shared_from_this(), page);
        }

        lib::mutex lock;

        stat stat;
        bool can_mmap;

        std::shared_ptr<mount> mount;
        std::shared_ptr<ops> op;

        inode(std::shared_ptr<ops> op) : op { op } { }

        virtual ~inode() = default;
    };

    struct node : std::enable_shared_from_this<node>
    {
        static std::shared_ptr<node> root(bool from_sched = false);

        lib::mutex lock;

        std::string name;
        std::string symlinked_to;

        std::shared_ptr<inode> inode;

        std::weak_ptr<node> parent;
        lib::map::flat_hash<std::string_view, std::shared_ptr<node>> children;
        std::weak_ptr<node> children_redirect;

        std::shared_ptr<node> mountpoint;

        inline decltype(children) &get_children()
        {
            // children_redirect is either always expired, or always set.
            // if it was initially set to a valid node, it'll never expire.
            if (!children_redirect.expired())
                return children_redirect.lock()->get_children();
            return children;
        }

        inline std::shared_ptr<node> me()
        {
            // literally me fr
            if (mountpoint)
                return mountpoint->me();
            return shared_from_this();
        }

        static constexpr auto symloop_max = 40;
        expect<std::shared_ptr<node>> reduce(std::size_t symlink_depth = symloop_max);
    };

    struct resolve_res
    {
        std::shared_ptr<node> parent;
        std::shared_ptr<node> target;
    };

    bool register_fs(std::unique_ptr<filesystem> fs);
    auto find_fs(std::string_view name) -> expect<std::reference_wrapper<std::unique_ptr<filesystem>>>;

    auto mount_for(std::shared_ptr<node> parent, lib::path path) -> expect<std::shared_ptr<struct mount>>;
    auto resolve(std::shared_ptr<struct mount> fs, std::shared_ptr<node> parent, lib::path path) -> expect<resolve_res>;

    auto mount(std::shared_ptr<node> parent, lib::path source, lib::path target, std::string_view fsname) -> expect<void>;
    auto unmount(std::shared_ptr<node> parent, lib::path target) -> expect<void>;

    auto create(std::shared_ptr<node> parent, lib::path path, mode_t mode) -> expect<std::shared_ptr<node>>;
    auto symlink(std::shared_ptr<node> parent, lib::path path, lib::path target) -> expect<std::shared_ptr<node>>;
    auto link(std::shared_ptr<node> parent, lib::path path, std::shared_ptr<node> tgtparent, lib::path target) -> expect<std::shared_ptr<node>>;
    auto unlink(std::shared_ptr<node> parent, lib::path path) -> expect<void>;

    auto stat(std::shared_ptr<node> parent, lib::path path) -> expect<stat>;
    bool populate(std::shared_ptr<node> parent, std::string_view name = "");

    initgraph::stage *root_mounted_stage();
} // export namespace vfs