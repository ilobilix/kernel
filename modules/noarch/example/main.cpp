// Copyright (C) 2024-2025  ilobilo

import lib;

import std;
import std.compat;

namespace external
{
    __attribute__((constructor))
    void func()
    {
        lib::info("YAYAYAYAYAYAYAYAYYAYAYAY!");
        lib::print("abcd");
        lib::println("efg");
        lib::debug("hijklmnop");
    }

    bool init() { lib::error("Hello, World!"); return true; }
    bool fini() { lib::warn("Goodbye, World!"); return true; }
} // namespace external

define_module {
    "example", "an example module demonstrating blah blah blah description goes here",
    mod::generic { .init = external::init, .fini = external::fini },
    mod::deps { "test1", "test2" }
};