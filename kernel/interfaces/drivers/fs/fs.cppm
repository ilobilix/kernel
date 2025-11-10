// Copyright (C) 2024-2025  ilobilo

export module drivers.fs;

export import drivers.fs.dev;
export import drivers.fs.devtmpfs;
export import drivers.fs.tmpfs;
import lib;

export namespace fs
{
    lib::initgraph::stage *registered_stage();
} // export namespace fs