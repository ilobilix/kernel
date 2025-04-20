// Copyright (C) 2024-2025  ilobilo

module;

#include <limine.h>

export module drivers.framebuffer;
import cppstd;

export namespace frm
{
    using framebuffer = limine_framebuffer;
    std::vector<framebuffer> framebuffers;

    void init();
} // export namespace frm