// Copyright (C) 2024-2026  ilobilo

export module vinput:spec;

import std;

export namespace vinput
{
    enum config_select : std::uint8_t
    {
        cfg_unset     = 0x00,
        cfg_id_name   = 0x01,
        cfg_id_serial = 0x02,
        cfg_id_devids = 0x03,
        cfg_prop_bits = 0x10,
        cfg_ev_bits   = 0x11,
        cfg_abs_info  = 0x12
    };

    struct absinfo_t
    {
        std::uint32_t min;
        std::uint32_t max;
        std::uint32_t fuzz;
        std::uint32_t flat;
        std::uint32_t res;
    };

    struct devids_t
    {
        std::uint16_t bustype;
        std::uint16_t vendor;
        std::uint16_t product;
        std::uint16_t version;
    };

    struct config_t
    {
        std::uint8_t select;
        std::uint8_t subsel;
        std::uint8_t size;
        std::uint8_t reserved[5];
        union {
            char string[128];
            std::uint8_t bitmap[128];
            absinfo_t abs;
            devids_t ids;
        } info;
    };

    struct event_t
    {
        std::uint16_t type;
        std::uint16_t code;
        std::int32_t value;
    };
} // export namespace vinput
