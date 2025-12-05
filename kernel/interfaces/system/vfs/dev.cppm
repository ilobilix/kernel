// Copyright (C) 2024-2025  ilobilo

export module system.vfs.dev;

import system.vfs;
import lib;
import std;

export namespace vfs::dev
{
    inline constexpr std::uint32_t major(dev_t dev)
    {
        return
            ((dev & 0x00000000000FFF00ull) >> 8) |
            ((dev & 0xFFFFF00000000000ull) >> 32);
    }

    inline constexpr std::uint32_t minor(dev_t dev)
    {
        return
            (dev & 0x00000000000000FFull) |
            ((dev & 0x00000FFFFFF00000ull) >> 12);
    }

    inline constexpr dev_t makedev(std::uint32_t maj, std::uint32_t min)
    {
        return
            (maj & 0x00000FFFull) << 8 |
            (maj & 0xFFFFF000ull) << 32 |
            (min & 0x000000FFull) |
            (min & 0xFFFFFF00ull) << 12;
    }

    bool register_cdev(std::shared_ptr<vfs::ops> ops, dev_t dev);
    std::shared_ptr<vfs::ops> get_cdev_ops(dev_t dev);
} // export namespace vfs::dev