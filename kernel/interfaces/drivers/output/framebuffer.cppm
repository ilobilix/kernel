// Copyright (C) 2024-2025  ilobilo

module;

#include <limine.h>

export module drivers.output.framebuffer;
import std;

export namespace output::frm
{
    using framebuffer = limine_framebuffer;
    std::vector<framebuffer> framebuffers;

    void init();
} // export namespace output::frm