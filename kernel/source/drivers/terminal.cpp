// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>
#include <backends/fb.h>

module drivers.terminal;

import drivers.framebuffer;
import lib;
import std;

namespace term
{
#if !ILOBILIX_MAX_UACPI_POINTS
    namespace
    {
        std::uint32_t ansi_colours[] {
            0x00000000, 0x00AA0000, 0x0000AA00, 0x00AA5500,
            0x000000AA, 0x00AA00AA, 0x0000AAAA, 0x00AAAAAA
        };

        std::uint32_t ansi_bright_colours[] {
            0x00555555, 0x00FF5555, 0x0055FF55, 0x00FFFF55,
            0x005555FF, 0x00FF55FF, 0x0055FFFF, 0x00FFFFFF
        };

        char font[] {
            #embed "../../embed/font.bin"
        };
    } // namespace
#endif

    void init()
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        log::info("initialising the graphical terminal");

        for (auto &frm : frm::framebuffers)
        {
            auto ctx = flanterm_fb_init(
                std::malloc, [](void *ptr, std::size_t) { std::free(ptr); },
                reinterpret_cast<std::uint32_t *>(frm.address),
                frm.width, frm.height, frm.pitch,
                frm.red_mask_size, frm.red_mask_shift,
                frm.green_mask_size, frm.green_mask_shift,
                frm.blue_mask_size, frm.blue_mask_shift,
                nullptr, ansi_colours, ansi_bright_colours,
                nullptr, nullptr, nullptr, nullptr,
                font, 8, 16, 1,
                0, 0, 0
            );
            if (ctx == nullptr)
                lib::panic("could not initialise flanterm");

            terminals.emplace_back(ctx);
        }
#endif
    }
} // namespace term