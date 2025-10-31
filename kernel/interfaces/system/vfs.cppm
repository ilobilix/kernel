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

        target_is_a_dir,
        target_is_busy,

        dir_not_empty,

        different_filesystem,

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

    struct dentry;
    struct mount;

    struct path
    {
        std::shared_ptr<mount> mnt;
        std::shared_ptr<dentry> dentry;
    };

    struct filesystem
    {
        std::string name;

        struct instance
        {
            virtual auto create(std::shared_ptr<inode> &parent, std::string_view name, mode_t mode, std::shared_ptr<ops> ops = nullptr) -> expect<std::shared_ptr<inode>> = 0;

            virtual auto symlink(std::shared_ptr<inode> &parent, std::string_view name, lib::path target) -> expect<std::shared_ptr<inode>> = 0;
            virtual auto link(std::shared_ptr<inode> &parent, std::string_view name, std::shared_ptr<inode> target) -> expect<std::shared_ptr<inode>> = 0;
            virtual auto unlink(std::shared_ptr<inode> &inode) -> expect<void> = 0;

            virtual auto populate(std::shared_ptr<vfs::inode> &inode, std::string_view name = "") -> vfs::expect<std::list<std::pair<std::string, std::shared_ptr<vfs::inode>>>> = 0;
            virtual bool sync() = 0;
            virtual bool unmount(std::shared_ptr<mount> mnt) = 0;

            virtual ~instance() = default;
        };

        virtual auto mount(std::optional<std::shared_ptr<dentry>> src) const -> expect<std::shared_ptr<mount>> = 0;

        filesystem(std::string_view name) : name { name } { }
        virtual ~filesystem() = default;
    };

    struct mount
    {
        lib::locked_ptr<filesystem::instance, lib::mutex> fs;
        std::shared_ptr<dentry> root;
        std::optional<path> mounted_on;
    };

    struct inode : std::enable_shared_from_this<inode>
    {
        std::ssize_t read(std::size_t offset, std::span<std::byte> buffer)
        {
            lib::bug_on(op == nullptr);
            return op->read(shared_from_this(), offset, buffer);
        }

        std::ssize_t write(std::size_t offset, std::span<std::byte> buffer)
        {
            lib::bug_on(op == nullptr);
            return op->write(shared_from_this(), offset, buffer);
        }

        std::uintptr_t mmap(std::uintptr_t page, int flags)
        {
            lib::bug_on(op == nullptr);
            return op->mmap(shared_from_this(), page, flags);
        }

        bool munmap(std::uintptr_t page)
        {
            lib::bug_on(op == nullptr);
            return op->munmap(shared_from_this(), page);
        }

        lib::mutex lock;

        stat stat;
        bool can_mmap;

        std::shared_ptr<ops> op;

        inode(std::shared_ptr<ops> op) : op { op } { }

        virtual ~inode() = default;
    };

    struct dentry : std::enable_shared_from_this<dentry>
    {
        static std::shared_ptr<dentry> root(bool from_sched = false);

        lib::mutex lock;

        std::string name;
        std::string symlinked_to;

        std::shared_ptr<inode> inode;

        std::weak_ptr<dentry> parent;
        lib::map::flat_hash<std::string_view, std::shared_ptr<dentry>> children;

        std::list<std::weak_ptr<mount>> child_mounts;

        static constexpr auto symloop_max = 40;
        expect<std::shared_ptr<dentry>> reduce(std::size_t symlink_depth = symloop_max);
    };

    struct resolve_res
    {
        path parent;
        path target;
    };

    bool register_fs(std::unique_ptr<filesystem> fs);
    auto find_fs(std::string_view name) -> expect<std::reference_wrapper<std::unique_ptr<filesystem>>>;

    auto path_for(lib::path _path) -> expect<path>;
    auto resolve(std::optional<path> parent, lib::path path) -> expect<resolve_res>;

    auto mount(lib::path source, lib::path target, std::string_view fstype, int flags) -> expect<void>;
    auto unmount(lib::path target) -> expect<void>;

    auto create(std::optional<path> parent, lib::path _path, mode_t mode) -> expect<path>;
    auto symlink(std::optional<path> parent, lib::path src, lib::path target) -> expect<path>;
    auto link(std::optional<path> parent, lib::path src, std::optional<path> tgtparent, lib::path target) -> expect<path>;
    auto unlink(std::optional<path> parent, lib::path path) -> expect<void>;

    auto stat(std::optional<path> parent, lib::path path) -> expect<stat>;
    bool populate(path parent, std::string_view name = "");

    initgraph::stage *root_mounted_stage();
} // export namespace vfs