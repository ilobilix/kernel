// Copyright (C) 2024-2026  ilobilo

export module system.vfs.caps;

import system.sched.cred;
import lib;
import std;

export namespace vfs
{
    constexpr std::uint32_t cap_revision_mask = 0xFF000000;
    constexpr std::uint32_t cap_flags_mask = ~cap_revision_mask;
    constexpr std::uint32_t cap_flags_effective = 0x000001;

    constexpr std::uint32_t cap_revision_1 = 0x01000000;
    constexpr std::uint32_t cap_revision_2 = 0x02000000;
    constexpr std::uint32_t cap_revision_3 = 0x03000000;

    struct [[gnu::packed]] cap_data
    {
        std::uint32_t magic;
        struct {
            std::uint32_t permitted;
            std::uint32_t inheritable;
        } data[2];
        std::uint32_t rootid;
    };

    struct file_caps
    {
        sched::cap_t permitted = sched::cap_t::none;
        sched::cap_t inheritable = sched::cap_t::none;
        bool effective = false;
        uid_t rootid = 0;
    };

    std::optional<file_caps> parse_file_caps(std::span<const std::byte> data);
} // export namespace vfs
