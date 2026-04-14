// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev.tty;

import drivers.fs.devtmpfs;
import system.memory.virt;
import system.vfs;
import system.dev;
import arch;
import lib;
import cppstd;

namespace fs::dev::tty
{
    struct ops : vfs::ops
    {
        static std::shared_ptr<ops> singleton()
        {
            static auto instance = std::make_shared<ops>();
            return instance;
        }

        std::ssize_t read(std::shared_ptr<vfs::file> file, std::uint64_t offset, std::span<std::byte> buffer) override
        {
            arch::halt(false);
            return -1;
        }

        std::ssize_t write(std::shared_ptr<vfs::file> file, std::uint64_t offset, std::span<std::byte> buffer) override
        {
            log::print("{}", std::string_view { reinterpret_cast<const char *>(buffer.data()), buffer.size_bytes() });
            return buffer.size_bytes();
        }

        int ioctl(std::shared_ptr<vfs::file> file, unsigned long request, lib::may_be_uptr argp) override
        {
            lib::unused(file, request, argp);
            return 0;
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
            "vfs.dev.tty-registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::mounted_stage() },
        lib::initgraph::entail { registered_stage() },
        [] {
            using namespace ::dev;
            register_cdev(ops::singleton(), makedev(5, 1));
        }
    };
} // namespace fs::dev::tty