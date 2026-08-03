// Copyright (C) 2024-2026  ilobilo

module;

#include <flanterm.h>

export module drivers.output.terminal;

import drivers.output.framebuffer;
import lib;
import std;

export namespace output::term
{
    void write(flanterm_context *ctx, std::string_view str);

    flanterm_context *main();

    flanterm_context *create();
    void destroy(flanterm_context *ctx);

    void set_visible(flanterm_context *ctx, bool on);
    void dimensions(flanterm_context *ctx, std::size_t &cols, std::size_t &rows);

    void early_init();
    void init();
} // export namespace output::term
