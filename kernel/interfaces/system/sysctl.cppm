// Copyright (C) 2024-2026  ilobilo

export module system.sysctl;

import lib;
import std;

export namespace sysctl
{
    using read_fn = std::function<std::string()>;
    using write_fn = std::function<lib::expect<void>(std::string_view)>;
    using int_read_fn = std::function<int()>;
    using int_write_fn = std::function<lib::expect<void>(int)>;

    bool register_entry(std::string_view path, read_fn read, write_fn write, mode_t mode);
    bool register_ro(std::string_view path, read_fn read, mode_t mode = 0444);
    bool register_int(
        std::string_view path, int_read_fn read, int_write_fn write, mode_t mode = 0644
    );
    bool register_int_stub(std::string_view path, int default_value, mode_t mode = 0644);
} // export namespace sysctl
