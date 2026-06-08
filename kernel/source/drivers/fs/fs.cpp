// Copyright (C) 2024-2026  ilobilo

module drivers.fs;

namespace fs
{
    lib::initgraph::stage *registered_stage()
    {
        static lib::initgraph::stage stage {
            "vfs.fs.registered", lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task fs_task {
        "vfs.fs.register", lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            devpts::registered_stage(), devtmpfs::registered_stage(), procfs::registered_stage(),
            stubs::registered_stage(), sysfs::registered_stage(), tmpfs::registered_stage()
        },
        lib::initgraph::entail { registered_stage() }, [] { }
    };
} // namespace fs
