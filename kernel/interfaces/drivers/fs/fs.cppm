// Copyright (C) 2024-2025  ilobilo

export module drivers.fs;

export import drivers.fs.tmpfs;
import lib;

export namespace fs
{
    initgraph::stage *filesystems_registered_stage();
} // export namespace fs