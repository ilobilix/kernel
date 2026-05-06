// Copyright (C) 2024-2026  ilobilo

export module system.cmdline;

import lib;
import std;

export namespace cmdline
{
    std::string_view raw();

    std::optional<std::string_view> get(std::string_view req);
    bool has(std::string_view req);
} // export namespace cmdline
