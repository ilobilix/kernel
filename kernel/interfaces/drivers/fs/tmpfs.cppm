// Copyright (C) 2024-2025  ilobilo

export module drivers.fs.tmpfs;

import system.vfs;
import cppstd;

export namespace fs::tmpfs
{
    std::unique_ptr<vfs::filesystem> init();
} // export namespace fs::tmpfs

export namespace fs::devtmpfs
{
    std::unique_ptr<vfs::filesystem> init();
} // export namespace fs::devtmpfs