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

    namespace kallsyms
    {
        extern "C" const std::int32_t kallsyms_offsets[];
        extern "C" const std::uint8_t kallsyms_names[];

        extern "C" const std::uint32_t kallsyms_num_syms;
        extern "C" const std::uint64_t kallsyms_relative_base;

        extern "C" const char kallsyms_token_table[];
        extern "C" const std::uint16_t kallsyms_token_index[];

        extern "C" const std::uint32_t kallsyms_markers[];
        extern "C" const std::uint8_t kallsyms_seqs_of_names[];

        extern "C" char _skernel[];
        extern "C" char _ekernel[];

        bool in_kernel(std::uintptr_t addr)
        {
            return
                addr >= reinterpret_cast<std::uintptr_t>(_skernel) &&
                addr < reinterpret_cast<std::uintptr_t>(_ekernel);
        }

        std::uintptr_t sym_addr(std::size_t idx)
        {
            return kallsyms_relative_base + static_cast<std::uint32_t>(kallsyms_offsets[idx]);
        }

        std::size_t sym_offset(std::size_t pos)
        {
            const std::uint8_t *name = &kallsyms_names[kallsyms_markers[pos >> 8]];

            for (std::size_t i = 0; i < (pos & 0xFF); i++)
            {
                int len = *name;

                /*
                * If MSB is 1, it is a "big" symbol, so we need to look into
                * the next byte (and skip it, too).
                */
                if ((len & 0x80) != 0)
                    len = ((len & 0x7F) | (name[1] << 7)) + 1;

                name = name + len + 1;
            }

            return name - kallsyms_names;
        }

        auto lookup(std::uintptr_t addr, std::span<char> namebuf) -> const std::optional<std::uintptr_t>
        {
            if (!in_kernel(addr))
                return std::nullopt;

            std::uintptr_t sym_start = 0, sym_end = 0;
            std::size_t low = 0;

            for (std::size_t high = kallsyms_num_syms, mid; high - low > 1; )
            {
                mid = low + (high - low) / 2;
                if (sym_addr(mid) <= addr)
                    low = mid;
                else
                    high = mid;
            }

            while (low && sym_addr(low - 1) == sym_addr(low))
		        low--;

            sym_start = sym_addr(low);

            for (std::size_t i = low + 1; i < kallsyms_num_syms; i++)
            {
                if (sym_addr(i) > sym_start)
                {
                    sym_end = sym_addr(i);
                    break;
                }
            }

            if (sym_end == 0)
                return std::nullopt;

            auto expand_symbol = [&namebuf](std::size_t off)
            {
                const std::uint8_t *data = &kallsyms_names[off];
                int len = *data;
                data++;
                off++;

                if ((len & 0x80) != 0)
                {
                    len = (len & 0x7F) | (*data << 7);
                    data++;
                    off++;
                }

                off += len;

                std::size_t maxlen = std::min(namebuf.size(), static_cast<std::size_t>(KSYM_NAME_LEN));
                char *result = namebuf.data();

                bool skipped_first = false;
                const char *tptr;
                while (len)
                {
                    tptr = &kallsyms_token_table[kallsyms_token_index[*data]];
                    data++;
                    len--;
                    while (*tptr)
                    {
                        if (skipped_first)
                        {
                            if (maxlen <= 1)
                                goto exit;

                            *result = *tptr;
                            result++;
                            maxlen--;
                        }
                        else skipped_first = true;
                        tptr++;
                    }
                }

                exit:
                if (maxlen)
                    *result = 0;
            };

            expand_symbol(sym_offset(low));
            // std::size_t size = sym_end - sym_start;

            return addr - sym_start;
        }
    } // namespace kallsyms

    auto lookup(std::uintptr_t addr, std::span<char> namebuf) -> const std::optional<lookup_result>
    {
        auto search_in = [&](const symbol_table &table) -> std::pair<symbol, std::uintptr_t>
        {
            if (table.empty())
                return { empty, -1ul };

            auto it = std::find_if(table.cbegin(), table.cend(), [&addr](const symbol &sym) {
                return sym.address <= addr && addr <= (sym.address + sym.size);
            });

            if (it != table.end())
                return { *it, addr - it->address };

            return { empty, -1ul };
        };

        auto ret = kallsyms::lookup(addr, namebuf);
        if (!ret.has_value())
        {
            for (const auto &[name, mod] : mod::modules)
            {
                auto [sym, offset] = search_in(mod.symbols);
                if (sym != empty)
                {
                    if (namebuf.size() > 1)
                    {
                        const auto length = std::min(namebuf.size() - 1, sym.name.size());
                        std::strncpy(namebuf.data(), sym.name.data(), length);
                        namebuf[length] = 0;
                    }
                    return lookup_result { offset, name };
                }
            }
        }
        else return lookup_result { ret.value(), "kernel" };

        return std::nullopt;
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

            symbols.emplace(name, value, size);
        }

        return symbols;
    }

    bool kernel_loaded()
    {
        return !kernel_symbols.empty();
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