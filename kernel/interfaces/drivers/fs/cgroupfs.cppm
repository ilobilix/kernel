// Copyright (C) 2024-2026  ilobilo

export module drivers.fs.cgroupfs;

import system.vfs;
import lib;
import std;

export namespace fs::cgroupfs
{
    std::string proc_lines();
    lib::initgraph::stage *registered_stage();
} // export namespace fs::cgroupfs
