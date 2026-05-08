// Copyright (C) 2024-2026  ilobilo

export module system.sysctl;

import lib;
import std;

export namespace sysctl
{
    using read_fn = std::function<std::string ()>;
    using write_fn = std::function<lib::expect<void> (std::string_view)>;
    using int_read_fn = std::function<int ()>;
    using int_write_fn = std::function<lib::expect<void> (int)>;

    struct entry
    {
        read_fn read;
        write_fn write;
        mode_t mode;
    };

    bool register_entry(std::string path, read_fn read, write_fn write, mode_t mode);
    bool register_ro(std::string path, read_fn read, mode_t mode = 0444);
    bool register_int(std::string path, int_read_fn read, int_write_fn write, mode_t mode = 0644);
    bool register_int_stub(std::string path, int default_value, mode_t mode = 0644);

    std::optional<entry> find(std::string_view path);

    struct dirent
    {
        std::string name;
        bool is_dir;
    };
    std::vector<dirent> list(std::string_view dir);
    bool dir_exists(std::string_view path);
} // export namespace sysctl
