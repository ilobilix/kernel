// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.devtmpfs;

import system.vfs;
import lib;
import std;

export namespace fs::devtmpfs
{
    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *mounted_stage();
} // export namespace fs::devtmpfs