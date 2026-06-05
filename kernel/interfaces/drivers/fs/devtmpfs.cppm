// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.devtmpfs;

import system.vfs;
import lib;
import std;

export namespace fs::devtmpfs
{
    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *mounted_stage();

    lib::expect<void> create(lib::path path, mode_t mode, dev_t rdev = 0);
    lib::expect<void> remove(lib::path path);
} // export namespace fs::devtmpfs
