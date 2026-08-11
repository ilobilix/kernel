// Copyright (C) 2024-2026  ilobilo

export module lib:ioctl;

import std;

export namespace lib::ioc
{
    constexpr std::uint32_t nrbits = 8;
    constexpr std::uint32_t typebits = 8;
    constexpr std::uint32_t sizebits = 14;
    constexpr std::uint32_t dirbits = 2;

    constexpr std::uint32_t nrmask = (1u << nrbits) - 1;
    constexpr std::uint32_t typemask = (1u << typebits) - 1;
    constexpr std::uint32_t sizemask = (1u << sizebits) - 1;
    constexpr std::uint32_t dirmask = (1u << dirbits) - 1;

    constexpr std::uint32_t nrshift = 0;
    constexpr std::uint32_t typeshift = nrshift + nrbits;
    constexpr std::uint32_t sizeshift = typeshift + typebits;
    constexpr std::uint32_t dirshift = sizeshift + sizebits;

    constexpr std::uint32_t none = 0u;
    constexpr std::uint32_t write = 1u;
    constexpr std::uint32_t read = 2u;

    constexpr std::uint32_t make_ioc(
        std::uint32_t dir, std::uint32_t type, std::uint32_t nr, std::uint32_t size
    )
    {
        return (dir << dirshift) | (type << typeshift) | (nr << nrshift) | (size << sizeshift);
    }

    constexpr std::uint32_t make_io(std::uint32_t type, std::uint32_t nr)
    {
        return make_ioc(none, type, nr, 0);
    }

    template<typename Type>
    constexpr std::uint32_t make_ior(std::uint32_t type, std::uint32_t nr)
    {
        return make_ioc(read, type, nr, sizeof(Type));
    }

    template<typename Type>
    constexpr std::uint32_t make_iow(std::uint32_t type, std::uint32_t nr)
    {
        return make_ioc(write, type, nr, sizeof(Type));
    }

    template<typename Type>
    constexpr std::uint32_t make_iowr(std::uint32_t type, std::uint32_t nr)
    {
        return make_ioc(read | write, type, nr, sizeof(Type));
    }

    constexpr std::uint32_t make_dir(std::uint32_t ioc_nr)
    {
        return (ioc_nr >> dirshift) & dirmask;
    }

    constexpr std::uint32_t make_type(std::uint32_t ioc_nr)
    {
        return (ioc_nr >> typeshift) & typemask;
    }

    constexpr std::uint32_t make_nr(std::uint32_t ioc_nr)
    {
        return (ioc_nr >> nrshift) & nrmask;
    }

    constexpr std::uint32_t make_size(std::uint32_t ioc_nr)
    {
        return (ioc_nr >> sizeshift) & sizemask;
    }

    constexpr std::uint32_t in = (write << dirshift);
    constexpr std::uint32_t out = (read << dirshift);
    constexpr std::uint32_t inout = ((write | read) << dirshift);
    constexpr std::uint32_t size_mask = (sizemask << sizeshift);
    constexpr std::uint32_t size_shift_v = sizeshift;
} // export namespace lib::ioc
