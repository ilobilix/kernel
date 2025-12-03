// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev;

import drivers.initramfs;
import system.dev;
import system.vfs;
import magic_enum;
import lib;
import std;

namespace fs::dev
{
    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::stage *populated_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.populated",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task dev_task
    {
        "vfs.dev.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            mem::registered_stage(),
            tty::registered_stage()
        },
        lib::initgraph::entail { registered_stage() },
        [] { }
    };

    lib::initgraph::task populate_task
    {
        "vfs.dev.populate",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            registered_stage(),
            initramfs::extracted_stage()
        },
        lib::initgraph::entail { populated_stage() },
        [] {
            auto create = [](std::string_view path, mode_t mode, dev_t dev)
            {
                auto ret = vfs::create(std::nullopt, path, mode, dev);
                lib::panic_if(
                    !ret && ret.error() != vfs::error::already_exists,
                    "dev: failed to create a character device file '{}': {}", path, magic_enum::enum_name(ret.error())
                );
            };

            using namespace ::dev;
            create("/dev/null", stat::s_ifchr | 0666, makedev(1, 3));
            create("/dev/zero", stat::s_ifchr | 0666, makedev(1, 5));
            create("/dev/full", stat::s_ifchr | 0666, makedev(1, 7));
            create("/dev/random", stat::s_ifchr | 0666, makedev(1, 8));
            create("/dev/urandom", stat::s_ifchr | 0666, makedev(1, 9));
        }
    };
} // namespace fs::dev