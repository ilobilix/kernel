// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.memory;
import boot;
import lib;
import std;

namespace bin::elf
{
    namespace
    {
        symbol_table kernel_symbols;

        auto get_symbols(std::uintptr_t address) -> symbol_table
        {
            auto elf = reinterpret_cast<Elf64_Ehdr *>(address);
            auto sections = reinterpret_cast<Elf64_Shdr *>(address + elf->e_shoff);

            const char *strtab = nullptr;
            Elf64_Sym *symtab = nullptr;
            std::size_t entries = 0;

            for (std::size_t i = 0; i < elf->e_shnum; i++)
            {
                const auto &section = sections[i];
                if (section.sh_type == SHT_SYMTAB)
                {
                    symtab = reinterpret_cast<Elf64_Sym *>(address + section.sh_offset);
                    entries = section.sh_size / section.sh_entsize;
                }
                else if (section.sh_type == SHT_STRTAB)
                    strtab = reinterpret_cast<const char *>(address + section.sh_offset);
            }

            symbol_table symbols { };

            for (std::size_t i = 0; i < entries; i++)
            {
                const auto sym = symtab[i];
                const std::string_view name { &strtab[sym.st_name] };
                if (sym.st_shndx == SHN_UNDEF || name.empty())
                    continue;

#if defined(__aarch64__)
                if (name.starts_with("$x") || name.starts_with("$d"))
                    continue;
#endif

                const std::uintptr_t value = sym.st_value < address ? sym.st_value + address : sym.st_value;
                const std::size_t size = sym.st_size;
                const std::uint8_t type = ELF64_ST_TYPE(sym.st_info);
                if (type != STT_FUNC && type != STT_OBJECT)
                    continue;

                symbols.emplace_back(name, value, size, type);
            }

            std::ranges::sort(symbols, std::less { });
            return symbols;
        }
    } // namespace

    const std::tuple<symbol, std::uintptr_t, std::string_view> lookup(std::uintptr_t addr, std::uint8_t type)
    {
        auto search_in = [&](const symbol_table &table) -> std::pair<symbol, std::uintptr_t>
        {
            if (table.empty())
                return { empty_symbol, -1ul };

            auto prev = table.front();
            auto end = table.back();

            if (addr < prev.address || addr > (end.address + end.size))
                return { empty_symbol, -1ul };

            for (const auto &entry : table)
            {
                if (entry.address >= addr && prev.address <= addr && prev.type == type)
                    return { prev, addr - prev.address };
                prev = entry;
            }
            return { empty_symbol, -1ul };
        };

        // TODO: modules
        auto [sym, offset] = search_in(kernel_symbols);
        return { sym, offset, "kernel" };
    }

    void init_ksyms()
    {
        auto kfile = reinterpret_cast<std::uintptr_t>(boot::requests::kernel_file.response->executable_file->address);
        kernel_symbols = get_symbols(kfile);
    }
} // namespace bin::elf