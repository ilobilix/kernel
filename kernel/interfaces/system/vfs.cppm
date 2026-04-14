// Copyright (C) 2024-2025  ilobilo

export module system.vfs;

import lib;
import std;

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

    struct node;
    struct filesystem
    {
        std::string name;

        struct instance
        {
            lib::mutex lock;

            std::weak_ptr<node> root;
            std::weak_ptr<node> mounted_on;
            std::shared_ptr<filesystem> fs;

            virtual auto create(std::shared_ptr<node> parent, std::string_view name, mode_t mode) -> expect<std::shared_ptr<node>> = 0;
            virtual auto symlink(std::shared_ptr<node> parent, std::string_view name, lib::path target) -> expect<std::shared_ptr<node>> = 0;
            virtual auto link(std::shared_ptr<node> parent, std::string_view name, std::shared_ptr<node> target) -> expect<std::shared_ptr<node>> = 0;
            virtual auto unlink(std::shared_ptr<node> node) -> expect<void> = 0;

            virtual bool populate(std::shared_ptr<node> node, std::string_view name = "") = 0;
            virtual bool sync() = 0;
            virtual bool unmount() = 0;

            virtual ~instance() = default;
        };

        virtual std::pair<std::shared_ptr<instance>, std::shared_ptr<node>> mount(std::shared_ptr<node>) const = 0;

        filesystem(std::string_view name) : name { name } { }
        virtual ~filesystem() = default;
    };

    struct backing
    {
        struct ops
        {
            virtual std::ssize_t read(std::shared_ptr<backing> self, std::size_t offset, std::span<std::byte> buffer) = 0;
            virtual std::ssize_t write(std::shared_ptr<backing> self, std::size_t offset, std::span<std::byte> buffer) = 0;

            virtual ~ops() = default;
        };

        lib::mutex lock;

        stat stat;
        std::atomic_size_t refcount;
        std::shared_ptr<ops> op;

        backing(std::shared_ptr<ops> op) : refcount { 1 }, op { op } { }

        virtual ~backing() = default;
    };

    struct node : std::enable_shared_from_this<node>
    {
        static std::shared_ptr<node> root(bool from_sched = false);

        lib::mutex lock;

        std::string name;
        std::string symlinked_to;

        std::shared_ptr<backing> backing;

        std::weak_ptr<node> parent;
        lib::map::flat_hash<std::string_view, std::shared_ptr<node>> children;

        std::shared_ptr<filesystem::instance> fs;
        std::shared_ptr<node> mountpoint;

        inline std::shared_ptr<node> me()
        {
            if (mountpoint)
                return mountpoint->me();
            else
                return shared_from_this();
        }

        inline lib::path to_path() const
        {
            if (!parent.expired())
            {
                auto ptr = parent.lock();
                if (ptr.get() == this)
                    return name;
                return ptr->to_path() / name;
            }
            else return name;
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

    auto resolve(std::shared_ptr<node> parent, lib::path path) -> expect<resolve_res>;

    auto mount(std::shared_ptr<node> parent, lib::path source, lib::path target, std::string_view fsname) -> expect<void>;
    auto unmount(std::shared_ptr<node> parent, lib::path target) -> expect<void>;

    auto create(std::shared_ptr<node> parent, lib::path path, mode_t mode) -> expect<std::shared_ptr<node>>;
    auto symlink(std::shared_ptr<node> parent, lib::path path, lib::path target) -> expect<std::shared_ptr<node>>;
    auto link(std::shared_ptr<node> parent, lib::path path, std::shared_ptr<node> tgtparent, lib::path target) -> expect<std::shared_ptr<node>>;
    auto unlink(std::shared_ptr<node> parent, lib::path path) -> expect<void>;

    auto stat(std::shared_ptr<node> parent, lib::path path) -> expect<stat>;
} // namespace vfs