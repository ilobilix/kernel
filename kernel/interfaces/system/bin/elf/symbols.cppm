// Copyright (C) 2024-2025  ilobilo

export module system.bin.elf:symbols;

import lib;
import std;

export namespace bin::elf::sym
{
    struct symbol
    {
        std::string_view name;
        std::uintptr_t address;
        std::size_t size;

        constexpr auto operator<=>(const symbol &rhs) const { return address <=> rhs.address; }
        constexpr auto operator<=>(std::uintptr_t rhs) const { return address <=> rhs; }

        constexpr bool operator==(const symbol &rhs) const
        {
            return
                name == rhs.name &&
                address == rhs.address &&
                size == rhs.size;
        }
    };
    constexpr symbol empty { { }, -1ul, 0 };

    using symbol_table = lib::btree::set<symbol>;

    struct lookup_result { std::uintptr_t offset; std::string_view from; };
    auto lookup(std::uintptr_t addr, std::span<char> namebuf) -> const std::optional<lookup_result>;
    std::uintptr_t klookup(std::string_view name);

    auto get_symbols(const char *strtab, const std::uint8_t *symtab, std::size_t syment, std::size_t symsz, std::uintptr_t offset = 0) -> symbol_table;
} // export namespace bin::elf::sym