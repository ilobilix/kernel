// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev;

import system.vfs;
import lib;

namespace fs::dev
{
    lib::initgraph::stage *initialised_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.dev.initialised",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task dev_task
    {
        "vfs.dev.initialised",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { mem::initialised_stage(), tty::initialised_stage() },
        lib::initgraph::entail { initialised_stage() },
        [] { }
    };
} // namespace fs::dev