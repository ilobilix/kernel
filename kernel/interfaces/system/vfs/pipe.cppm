// Copyright (C) 2024-2026  ilobilo

export module system.vfs.pipe;

import system.vfs;
import lib;
import std;

export namespace vfs::pipe
{
    std::shared_ptr<vfs::ops> get_ops();
} // export namespace vfs::pipe
