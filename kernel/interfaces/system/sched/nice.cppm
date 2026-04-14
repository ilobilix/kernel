// Copyright (C) 2024-2026  ilobilo

export module system.sched:nice;

import lib;
import std;

export namespace sched
{
    using nice_t = lib::ranged<std::int8_t, -20, 19>;
    constexpr nice_t default_nice = 0;

    // these values are from linux
    inline constexpr std::size_t nice_to_weight(nice_t nice)
    {
        constexpr std::size_t table[40]
        {
            88761, 71755, 56483, 46273, 36291,
            29154, 23254, 18705, 14949, 11916,
            9548,  7620,  6100,  4904,  3906,
            3121,  2501,  1991,  1586,  1277,
            1024,  820,   655,   526,   423,
            335,   272,   215,   172,   137,
            110,   87,    70,    56,    45,
            36,    29,    23,    18,    15,
        };
        return table[nice + 20];
    }

    inline constexpr std::size_t nice_to_inv_weight(nice_t nice)
    {
        constexpr std::size_t table[40]
        {
            48388,     59856,     76040,     92818,    118348,
            147320,    184698,    229616,    287308,    360437,
            449829,    563644,    704093,    875809,   1099582,
            1376151,   1717300,   2157191,   2708050,   3363326,
            4194304,   5237765,   6557202,   8165337,  10153587,
            12820798,  15790321,  19976592,  24970740,  31350126,
            39045157,  49367440,  61356676,  76695844,  95443717,
            119304647, 148102320, 186735158, 238609294, 286331153,
        };
        return table[nice + 20];
    }
} // namespace export sched
