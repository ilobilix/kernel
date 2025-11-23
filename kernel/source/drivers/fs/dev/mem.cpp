// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev.mem;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.dev;
import system.vfs;
import boot;
import lib;
import cppstd;

namespace fs::dev::mem
{
    struct null_ops : vfs::ops
    {
        static std::shared_ptr<null_ops> singleton()
        {
            static auto instance = std::make_shared<null_ops>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return 0;
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            return buffer.size_bytes();
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::file> file, bool priv) override
        {
            lib::unused(file, priv);
            return nullptr;
        }

        bool sync() override { return true; }
    };

    struct zero_ops : vfs::ops
    {
        static std::shared_ptr<zero_ops> singleton()
        {
            static auto instance = std::make_shared<zero_ops>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            buffer.fill(0, buffer.size_bytes());
            return buffer.size_bytes();
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return buffer.size_bytes();
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::file> file, bool priv) override
        {
            lib::unused(file, priv);
            return nullptr;
        }

        bool sync() override { return true; }
    };

    struct full_ops : vfs::ops
    {
        static std::shared_ptr<full_ops> singleton()
        {
            static auto instance = std::make_shared<full_ops>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            buffer.fill(0, buffer.size_bytes());
            return buffer.size_bytes();
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return (errno = ENOSPC, -1);
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::file> file, bool priv) override
        {
            lib::unused(file, priv);
            return nullptr;
        }

        bool sync() override { return true; }
    };

    // TODO
    struct random_dev : vfs::ops
    {
        static std::shared_ptr<random_dev> singleton()
        {
            static auto instance = std::make_shared<random_dev>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset);
            lib::random_bytes(buffer);
            return buffer.size_bytes();
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, lib::maybe_uspan<std::byte> buffer) override
        {
            lib::unused(file, offset, buffer);
            return buffer.size_bytes();
        }

        bool trunc(std::shared_ptr<vfs::file> file, std::size_t size) override
        {
            lib::unused(file, size);
            return true;
        }

        std::shared_ptr<vmm::object> map(std::shared_ptr<vfs::file> file, bool priv) override
        {
            lib::unused(file, priv);
            return nullptr;
        }

        bool sync() override { return true; }
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
            using namespace ::dev;
            register_cdev(null_ops::singleton(), makedev(1, 3));
            register_cdev(zero_ops::singleton(), makedev(1, 5));
            register_cdev(full_ops::singleton(), makedev(1, 7));

            const auto rand = random_dev::singleton();
            register_cdev(rand, makedev(1, 8));
            register_cdev(rand, makedev(1, 9));
        }
    };
} // namespace fs::dev::mem