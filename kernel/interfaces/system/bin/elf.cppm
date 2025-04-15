// Copyright (C) 2024-2025  ilobilo

export module system.bin.elf;

import lib;
import std;

export namespace bin::elf
{
    struct symbol
    {
        std::string_view name;
        std::uintptr_t address;
        std::size_t size;
        std::uint8_t type;

        constexpr auto operator<=>(const symbol &rhs) const { return address <=> rhs.address; }
        constexpr auto operator<=>(std::uintptr_t rhs) const { return address <=> rhs; }
        constexpr bool operator==(const symbol &rhs) const
        {
            return
                name == rhs.name &&
                address == rhs.address &&
                size == rhs.size &&
                type == rhs.type;
        }
    };
    constexpr symbol empty_symbol { { }, -1ul, 0, 0 };

    using symbol_table = std::vector<symbol>;
    const std::tuple<symbol, std::uintptr_t, std::string_view> lookup(std::uintptr_t addr, std::uint8_t type);

    void init_ksyms();
} // export namespace bin::elf