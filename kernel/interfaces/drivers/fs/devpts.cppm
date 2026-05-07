// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.devpts;

import system.vfs;
import lib;
import std;

export namespace fs::devpts
{
    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *mounted_stage();

    lib::expect<vfs::path> attach_slave(std::uint32_t minor, mode_t mode, dev_t rdev);
    lib::expect<void> detach_slave(std::uint32_t minor);
} // export namespace fs::devpts
