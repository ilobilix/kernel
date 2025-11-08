// Copyright (C) 2024-2025  ilobilo

module drivers.fs;

import system.scheduler;
import system.vfs;
import lib;

namespace fs
{
    initgraph::stage *filesystems_registered_stage()
    {
        static initgraph::stage stage { "builtin-filesystems-registered" };
        return &stage;
    }

    initgraph::task fs_task
    {
        "register-builtin-filesystems",
        initgraph::require { },
        initgraph::entail { filesystems_registered_stage() },
        [] {
            lib::bug_on(!vfs::register_fs(tmpfs::init()));
            lib::bug_on(!vfs::register_fs(devtmpfs::init()));
        }
    };
} // namespace fs