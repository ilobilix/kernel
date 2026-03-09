// Copyright (C) 2024-2026  ilobilo

module drivers.fs.dev.mem;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.vfs;
import system.vfs.dev;
import magic_enum;
import boot;
import lib;
import std;

namespace fs::dev::mem
{
    struct null_ops : vfs::ops
    {
        static std::shared_ptr<null_ops> singleton()
        {
            static auto instance = std::make_shared<null_ops>();
            return instance;
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return 0;
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            return buffer.size_bytes();
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }
    };

    struct zero_ops : vfs::ops
    {
        static std::shared_ptr<zero_ops> singleton()
        {
            static auto instance = std::make_shared<zero_ops>();
            return instance;
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            buffer.fill(0, buffer.size_bytes());
            return buffer.size_bytes();
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return buffer.size_bytes();
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }
    };

    struct full_ops : vfs::ops
    {
        static std::shared_ptr<full_ops> singleton()
        {
            static auto instance = std::make_shared<full_ops>();
            return instance;
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            buffer.fill(0, buffer.size_bytes());
            return buffer.size_bytes();
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return std::unexpected { lib::err::no_space_left };
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }
    };

    // TODO
    struct random_dev : vfs::ops
    {
        static std::shared_ptr<random_dev> singleton()
        {
            static auto instance = std::make_shared<random_dev>();
            return instance;
        }

        lib::expect<std::size_t> read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            lib::random_bytes(buffer);
            return buffer.size_bytes();
        }

        lib::expect<std::size_t> write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return buffer.size_bytes();
        }

        lib::expect<void> trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return { };
        }
    };

    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.memfiles-registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task memfiles_task
    {
        "vfs.dev.memfiles.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::mounted_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            using namespace vfs::dev;
            register_dev_ops(makedev(1, 3), null_ops::singleton());
            register_dev_ops(makedev(1, 5), zero_ops::singleton());
            register_dev_ops(makedev(1, 7), full_ops::singleton());

            const auto rand = random_dev::singleton();
            register_dev_ops(makedev(1, 8), rand);
            register_dev_ops(makedev(1, 9), rand);

            auto create = [](std::string_view path, mode_t mode, dev_t dev)
            {
                const auto ret = vfs::create(std::nullopt, path, mode, dev);
                if (!ret && ret.error() != lib::err::already_exists)
                {
                    lib::panic(
                        "mem: failed to create '{}': {}",
                        path, magic_enum::enum_name(ret.error())
                    );
                }
            };

            using namespace vfs::dev;
            create("/dev/null", stat::s_ifchr | 0666, makedev(1, 3));
            create("/dev/zero", stat::s_ifchr | 0666, makedev(1, 5));
            create("/dev/full", stat::s_ifchr | 0666, makedev(1, 7));
            create("/dev/random", stat::s_ifchr | 0666, makedev(1, 8));
            create("/dev/urandom", stat::s_ifchr | 0666, makedev(1, 9));
        }
    };
} // namespace fs::dev::mem
