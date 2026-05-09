// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.dev.pty;

import system.vfs;
import lib;
import std;

export namespace fs::dev::pty
{
    struct pair;

    lib::expect<std::shared_ptr<pair>> alloc();
    void release(std::uint32_t minor);

    lib::initgraph::stage *registered_stage();
} // export namespace fs::dev::pty
