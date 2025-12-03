// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>

export module drivers.output.terminal;

import drivers.output.framebuffer;
import lib;
import std;

export namespace output::term
{
    void write(flanterm_context *ctx, std::string_view str);
    void write(flanterm_context *ctx, char chr);

    flanterm_context *main();

    void early_init();
    void init();
} // export namespace output::term