// Copyright (C) 2024-2025  ilobilo

module;

#include <limine.h>

module drivers.output.framebuffer;

import boot;
import lib;
import std;

namespace output::frm
{
    void init()
    {
        const auto frms = boot::requests::framebuffer.response->framebuffers;
        const auto num = boot::requests::framebuffer.response->framebuffer_count;

        log::info("available framebuffers: {}", num);

        for (std::size_t i = 0; i < num; i++)
        {
            const auto &entry = frms[i];
            auto &frm = framebuffers.emplace_back(*entry);

            frm.edid = new std::byte[entry->edid_size];
            std::memcpy(frm.edid, entry->edid, entry->edid_size);

            frm.modes = new limine_video_mode *[frm.mode_count];
            for (std::size_t ii = 0; ii < frm.mode_count; ii++)
                frm.modes[ii] = new limine_video_mode(*entry->modes[ii]);
        }
    }
} // namespace output::frm