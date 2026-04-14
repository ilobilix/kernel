// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>
#include <backends/fb.h>
#if !ILOBILIX_MAX_UACPI_POINTS
#  include <lib/unifont.h>
#endif


module drivers.terminal;

import drivers.framebuffer;
import lib;
import std;

namespace term
{
    void *alloc(std::size_t size) { return std::malloc(size); }
    void free(void *ptr, std::size_t) { std::free(ptr); }

    void init()
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        log::info("initializing the graphical terminal");

        auto font = const_cast<void *>(reinterpret_cast<const void *>(unifont));
        for (auto &frm : frm::framebuffers)
        {
            auto ctx = flanterm_fb_init(
                alloc, free,
                reinterpret_cast<std::uint32_t *>(frm.address),
                frm.width, frm.height, frm.pitch,
                frm.red_mask_size, frm.red_mask_shift,
                frm.green_mask_size, frm.green_mask_shift,
                frm.blue_mask_size, frm.blue_mask_shift,
                nullptr,
                nullptr, nullptr,
                nullptr, nullptr,
                nullptr, nullptr,
                font, UNIFONT_WIDTH, UNIFONT_HEIGHT, 1,
                0, 0,
                0
            );
            if (ctx == nullptr)
                lib::panic("could not initialise flanterm");

            terminals.push_back(reinterpret_cast<terminal *>(ctx));
        }
#endif
    }
} // namespace term