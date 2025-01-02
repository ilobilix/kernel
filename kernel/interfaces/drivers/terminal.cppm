// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>

export module drivers.terminal;

import drivers.framebuffer;
import lib;
import std;

export namespace term
{
    struct terminal : flanterm_context
    {
        void write(std::string_view str)
        {
            flanterm_write(this, str.data(), str.length());
        }
        void write(char chr)
        {
            flanterm_write(this, &chr, 1);
        }
    };
    static_assert(sizeof(terminal) == sizeof(flanterm_context));

    std::vector<terminal *> terminals;
    inline auto *main() { return terminals.empty() ? nullptr : terminals.back(); }

    void init();
} // export namespace term