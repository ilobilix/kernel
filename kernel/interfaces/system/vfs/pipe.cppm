// Copyright (C) 2024-2026  ilobilo

export module system.vfs.pipe;

import system.vfs;
import lib;
import std;

export namespace vfs::pipe
{
    lib::expect<std::pair<int, int>> create_pair(int flags);

    lib::expect<std::size_t> get_size(std::shared_ptr<vfs::file> file);
    lib::expect<std::size_t> set_size(std::shared_ptr<vfs::file> file, std::size_t size);
} // export namespace vfs::pipe
