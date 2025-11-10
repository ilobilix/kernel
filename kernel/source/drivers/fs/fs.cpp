// Copyright (C) 2024-2025  ilobilo

module drivers.fs;

import drivers.fs.devtmpfs;
import drivers.fs.tmpfs;
import lib;

namespace fs
{
    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "vfs.fs.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task fs_task
    {
        "vfs.fs.register",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { tmpfs::registered_stage(), devtmpfs::registered_stage() },
        lib::initgraph::entail { registered_stage() },
        [] { }
    };
} // namespace fs