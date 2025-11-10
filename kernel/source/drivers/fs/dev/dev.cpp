// Copyright (C) 2024-2025  ilobilo

module drivers.fs.dev;

import system.vfs;
import lib;

namespace fs::dev
{
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
        "vfs.dev.populated",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { mem::initialised_stage(), tty::initialised_stage() },
        lib::initgraph::entail { populated_stage() },
        [] { }
    };
} // namespace fs::dev