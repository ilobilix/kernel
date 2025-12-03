// Copyright (C) 2024-2025  ilobilo

module;

#include <flanterm.h>
#include <flanterm_backends/fb.h>

module drivers.output.terminal;

import drivers.output.framebuffer;
import boot;
import lib;
import std;

namespace output::term
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

        signed char font[] {
            #embed "../../../embed/font.bin"
        };

        constinit void *early_addr = nullptr;
        constinit flanterm_context *early = nullptr;
        std::vector<flanterm_context *> contexts;
    } // namespace
#endif

    void write(flanterm_context *ctx, std::string_view str)
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        flanterm_write(ctx, str.data(), str.length());
#else
        lib::unused(ctx, str);
#endif
    }

    void write(flanterm_context *ctx, char chr)
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        flanterm_write(ctx, &chr, 1);
#else
        lib::unused(ctx, chr);
#endif
    }

    flanterm_context *main()
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        if (!contexts.empty())
            return contexts.back();
        else if (early)
            return early;
#endif
        return nullptr;
    }

    void early_init()
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        auto frm = boot::requests::framebuffer.response->framebuffers[0];
        early = flanterm_fb_init(
            nullptr, nullptr,
            reinterpret_cast<std::uint32_t *>(early_addr = frm->address),
            frm->width, frm->height, frm->pitch,
            frm->red_mask_size, frm->red_mask_shift,
            frm->green_mask_size, frm->green_mask_shift,
            frm->blue_mask_size, frm->blue_mask_shift,
            nullptr, ansi_colours, ansi_bright_colours,
            nullptr, nullptr, nullptr, nullptr,
            font, 8, 16, 1,
            0, 0, 0
        );
        if (early == nullptr)
            lib::panic("could not initialise flanterm");

        lib::info("initialised the graphical terminal");
#endif
    }

    void init()
    {
#if !ILOBILIX_MAX_UACPI_POINTS
        for (auto &frm : frm::framebuffers)
        {
            if (frm.address == early_addr)
            {
                contexts.push_back(early);
                continue;
            }

            auto ctx = flanterm_fb_init(
                lib::alloc, [](void *ptr, std::size_t) { lib::free(ptr); },
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

            contexts.push_back(ctx);
        }
#endif
    }
} // namespace output::term