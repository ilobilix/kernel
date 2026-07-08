// Copyright (C) 2024-2026  ilobilo

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.random;
import system.vfs;
import system.vfs.dev;
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

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset, buffer);
            return 0;
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset);
            return buffer.size_bytes();
        }
    };

    struct zero_ops : vfs::ops
    {
        static std::shared_ptr<zero_ops> singleton()
        {
            static auto instance = std::make_shared<zero_ops>();
            return instance;
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset);
            buffer.fill(0, buffer.size_bytes());
            return buffer.size_bytes();
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset, buffer);
            return buffer.size_bytes();
        }
    };

    struct full_ops : vfs::ops
    {
        static std::shared_ptr<full_ops> singleton()
        {
            static auto instance = std::make_shared<full_ops>();
            return instance;
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset);
            buffer.fill(0, buffer.size_bytes());
            return buffer.size_bytes();
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset, buffer);
            return std::unexpected { lib::err::no_space_left };
        }
    };

    struct random_dev : vfs::ops
    {
        static std::shared_ptr<random_dev> singleton()
        {
            static auto instance = std::make_shared<random_dev>();
            return instance;
        }

        lib::expect<std::size_t> read(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset);
            random::get_bytes(buffer);
            return buffer.size_bytes();
        }

        lib::expect<std::size_t> write(
            std::shared_ptr<vfs::file> file,
            std::uint64_t offset, lib::maybe_uspan<std::byte> buffer
        ) override
        {
            lib::unused(file, offset);

            if (!buffer.is_user())
            {
                random::add_entropy(buffer.span());
                return buffer.size_bytes();
            }

            lib::membuffer buf { std::min(buffer.size_bytes(), 1024uz) };
            std::size_t progress = 0;
            while (progress < buffer.size_bytes())
            {
                const auto chunk_size = std::min(
                    buffer.size_bytes() - progress, buf.size_bytes()
                );
                if (!buffer.subspan(progress, chunk_size).copy_to(buf.data()))
                    return std::unexpected { lib::err::invalid_address };

                random::add_entropy(std::span { buf.data(), chunk_size });
                progress += chunk_size;
            }
            return buffer.size_bytes();
        }
    };

    lib::initgraph::task memfiles_task
    {
        "vfs.dev.memfiles.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::registered_stage() },
        [] {
            using namespace vfs::dev;
            register_ops(makedev(1, 3), null_ops::singleton());
            register_ops(makedev(1, 5), zero_ops::singleton());
            register_ops(makedev(1, 7), full_ops::singleton());

            const auto rand = random_dev::singleton();
            register_ops(makedev(1, 8), rand);
            register_ops(makedev(1, 9), rand);

            auto create = [](std::string_view name, mode_t mode, dev_t dev)
            {
                const auto ret = fs::devtmpfs::create(name, mode, dev);
                if (!ret && ret.error() != lib::err::already_exists)
                {
                    lib::panic(
                        "mem: failed to create '/dev/{}': {}",
                        name, lib::error_name(ret.error())
                    );
                }
            };

            create("null", stat::s_ifchr | 0666, makedev(1, 3));
            create("zero", stat::s_ifchr | 0666, makedev(1, 5));
            create("full", stat::s_ifchr | 0666, makedev(1, 7));
            create("random", stat::s_ifchr | 0666, makedev(1, 8));
            create("urandom", stat::s_ifchr | 0666, makedev(1, 9));
        }
    };
} // namespace fs::dev::mem
