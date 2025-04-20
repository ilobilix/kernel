// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.memory;
import boot;
import lib;
import cppstd;

namespace bin::elf::sym
{
    namespace
    {
        symbol_table kernel_symbols;
    } // namespace

    auto lookup(std::uintptr_t addr, std::uint8_t type) -> const std::tuple<symbol, std::uintptr_t, std::string_view>
    {
        auto search_in = [&](const symbol_table &table) -> std::pair<symbol, std::uintptr_t>
        {
            if (table.empty())
                return { empty, -1ul };

            auto it = std::find_if(table.cbegin(), table.cend(), [&addr, &type](const symbol &sym) {
                return sym.type == type && sym.address <= addr && addr <= (sym.address + sym.size);
            });

            if (it != table.end())
                return { *it, addr - it->address };

            // auto prev = table.front();
            // auto end = table.back();

            // if (addr < prev.address || addr > (end.address + end.size))
            //     return { empty, -1ul };

            // for (const auto &entry : table)
            // {
            //     if (entry.address >= addr && prev.address <= addr && prev.type == type)
            //         return { prev, addr - prev.address };
            //     prev = entry;
            // }

            return { empty, -1ul };
        };

        auto [sym, offset] = search_in(kernel_symbols);
        if (sym == empty)
        {
            for (const auto &[name, mod] : mod::modules)
            {
                auto [sym, offset] = search_in(mod.symbols);
                if (sym != empty)
                    return { sym, offset, name };
            }
        }
        else return { sym, offset, "kernel" };

        return { empty, 0, "unknown" };
    }

    const symbol klookup(std::string_view name)
    {
        auto it = std::ranges::find_if(kernel_symbols, [&name](const symbol &sym) { return sym.name == name; });
        return it == kernel_symbols.end() ? empty : *it;
    }

    auto get_symbols(const char *strtab, const std::uint8_t *symtab, std::size_t syment, std::size_t symsz, std::uintptr_t offset) -> symbol_table
    {
        symbol_table symbols { };

        for (std::size_t i = 0; i < symsz / syment; i++)
        {
            auto sym = reinterpret_cast<const Elf64_Sym *>(symtab + i * syment);
            const std::string_view name { strtab + sym->st_name };
            if (sym->st_shndx == SHN_UNDEF || name.empty())
                continue;

#if defined(__aarch64__)
            if (name.starts_with("$x") || name.starts_with("$d"))
                continue;
#endif

            const std::uintptr_t value = offset + sym->st_value;
            const std::size_t size = sym->st_size;
            const std::uint8_t type = ELF64_ST_TYPE(sym->st_info);

            symbols.emplace_back(name, value, size, type);
        }

        std::sort(symbols.begin(), symbols.end(), std::less { });
        return symbols;
    }

    void load_kernel()
    {
        auto kfile = reinterpret_cast<std::uintptr_t>(boot::requests::kernel_file.response->executable_file->address);

        auto elf = reinterpret_cast<Elf64_Ehdr *>(kfile);
        auto sections = reinterpret_cast<Elf64_Shdr *>(kfile + elf->e_shoff);

        const char *strtab = nullptr;
        const std::uint8_t *symtab = nullptr;
        std::size_t syment = 0;
        std::size_t symsz = 0;

        for (std::size_t i = 0; i < elf->e_shnum; i++)
        {
            const auto &section = sections[i];
            if (section.sh_type == SHT_SYMTAB)
            {
                symtab = reinterpret_cast<const std::uint8_t *>(kfile + section.sh_offset);
                syment = section.sh_entsize;
                symsz = section.sh_size;
            }
            else if (section.sh_type == SHT_STRTAB)
                strtab = reinterpret_cast<const char *>(kfile + section.sh_offset);
        }

        kernel_symbols = get_symbols(strtab, symtab, syment, symsz);
    }
} // namespace bin::elf::sym