// Copyright (C) 2024-2026  ilobilo

export module system.vfs.dev;

import system.vfs;
import lib;
import std;

export namespace vfs::dev
{
    bool register_ops(dev_t rdev, std::shared_ptr<vfs::ops_t> ops);
    bool unregister_ops(dev_t rdev);

    lib::expect<std::shared_ptr<vfs::ops_t>> get_ops(dev_t rdev, mode_t mode);

    std::uint32_t alloc_char_major();
} // export namespace vfs::dev
