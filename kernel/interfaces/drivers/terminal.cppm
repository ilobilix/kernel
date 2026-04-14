// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>

export module drivers.terminal;

import drivers.framebuffer;
import lib;
import std;

export namespace term
{
    struct terminal
    {
        flanterm_context *ctx;

        void write(std::string_view str)
        {
            flanterm_write(ctx, str.data(), str.length());
        }
        void write(char chr)
        {
            flanterm_write(ctx, &chr, 1);
        }
    };

    std::vector<terminal> terminals;
    inline auto *main() { return terminals.empty() ? nullptr : &terminals.back(); }

    void init();
} // export namespace term