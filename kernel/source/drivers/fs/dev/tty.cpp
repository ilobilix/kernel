// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev.tty;

import drivers.fs.devtmpfs;
import system.vfs;
import lib;

namespace fs::dev::tty
{
    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.tty-initialised",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task tty_task
    {
        "vfs.dev.tty.initialise",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { devtmpfs::mounted_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] {
        }
    };
} // namespace fs::dev::tty