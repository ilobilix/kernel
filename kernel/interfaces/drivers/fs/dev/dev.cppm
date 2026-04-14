// Copyright (C) 2024-2025  ilobilo

export module drivers.fs.dev;

export import drivers.fs.dev.mem;
export import drivers.fs.dev.tty;
import lib;

export namespace fs::dev
{
    lib::initgraph::stage *registered_stage();
    lib::initgraph::stage *populated_stage();
} // export namespace fs::dev