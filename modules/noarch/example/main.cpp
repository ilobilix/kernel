// Copyright (C) 2024-2025  ilobilo

import lib;

__attribute__((constructor))
void func() { log::info("YAYAYAYAYAYAYAYAYYAYAYAY!"); }

bool init() { log::info("Hello, World!"); return true; }
bool fini() { log::info("Goodbye, World!"); return true; }

define_module {
    "example", "an example module demonstrating blah blah blah description goes here",
    mod::generic { .init = init, .fini = fini },
    mod::deps { "test1", "test2" }
};