// Copyright (C) 2024-2025  ilobilo

export module system.bin.elf:symbols;

import lib;
import cppstd;

export namespace bin::elf::sym
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
    constexpr symbol empty { { }, -1ul, 0, 0 };

    using symbol_table = std::vector<symbol>;

    auto lookup(std::uintptr_t addr, std::uint8_t type) -> const std::tuple<symbol, std::uintptr_t, std::string_view>;
    const symbol klookup(std::string_view name);

    bool kernel_loaded();
    void load_kernel();
} // export namespace bin::elf::sym

namespace bin::elf::sym
{
    auto get_symbols(const char *strtab, const std::uint8_t *symtab, std::size_t syment, std::size_t symsz, std::uintptr_t offset = 0) -> symbol_table;
} // namespace bin::elf::sym