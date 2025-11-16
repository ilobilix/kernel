// Copyright (C) 2024-2025  ilobilo

import lib;

namespace external
{
    __attribute__((constructor))
    void func() { log::info("YAYAYAYAYAYAYAYAYYAYAYAY!"); }

    bool init() { log::info("Hello, World!"); return true; }
    bool fini() { log::info("Goodbye, World!"); return true; }
} // namespace external

define_module {
    "example", "an example module demonstrating blah blah blah description goes here",
    mod::generic { .init = external::init, .fini = external::fini },
    mod::deps { "test1", "test2" }
};