// Copyright (C) 2024-2026  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import drivers.fs.procfs;
import system.memory;
import boot;
import lib;
import fmt;
import std;

namespace bin::elf::sym
{
    namespace kallsyms
    {
        extern "C"
        {
            extern const std::int32_t kallsyms_offsets[];
            extern const std::uint8_t kallsyms_names[];

            extern const std::uint32_t kallsyms_num_syms;
            extern const std::uint64_t kallsyms_relative_base;

            extern const char kallsyms_token_table[];
            extern const std::uint16_t kallsyms_token_index[];

            extern const std::uint32_t kallsyms_markers[];
            extern const std::uint8_t kallsyms_seqs_of_names[];

            extern char _skernel[], _ekernel[];
        } // extern "C"

        namespace
        {
            bool in_kernel(std::uintptr_t addr)
            {
                return addr >= reinterpret_cast<std::uintptr_t>(_skernel) &&
                    addr < reinterpret_cast<std::uintptr_t>(_ekernel);
            }

            std::uintptr_t sym_addr(std::size_t idx)
            {
                return kallsyms_relative_base + static_cast<std::uint32_t>(kallsyms_offsets[idx]);
            }

            std::uint32_t sym_seq(std::size_t idx)
            {
                std::uint32_t seq = 0;
                for (std::size_t i = 0; i < 3; i++)
                    seq = (seq << 8) | kallsyms_seqs_of_names[3 * idx + i];
                return seq;
            }

            std::size_t sym_offset(std::size_t pos)
            {
                const std::uint8_t *name = &kallsyms_names[kallsyms_markers[pos >> 8]];

                for (std::size_t i = 0; i < (pos & 0xFF); i++)
                {
                    int len = *name;
                    if ((len & 0x80) != 0)
                        len = ((len & 0x7F) | (name[1] << 7)) + 1;

                    name = name + len + 1;
                }

                return name - kallsyms_names;
            }

            char expand_symbol(std::size_t off, std::span<char> namebuf)
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

                std::size_t maxlen =
                    std::min(namebuf.size(), static_cast<std::size_t>(KSYM_NAME_LEN));
                char *result = namebuf.data();

                char type = '?';
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
                        else
                        {
                            type = *tptr;
                            skipped_first = true;
                        }
                        tptr++;
                    }
                }

            exit:
                if (maxlen)
                    *result = 0;
                return type;
            };

            std::size_t next_offset(std::size_t off)
            {
                const auto data = &kallsyms_names[off];
                int len = *data;
                if ((len & 0x80) != 0)
                    return off + ((len & 0x7F) | (data[1] << 7)) + 2;
                return off + len + 1;
            }

            struct cache_t
            {
                std::string ksyms;
                std::size_t cursor_i = 0;
                std::size_t cursor_off = 0;
                bool ksyms_done = false;

                std::string modsyms;
                std::uint64_t mod_gen = -1ul;
            };
            lib::locker<cache_t, lib::spinlock> cache;
        } // namespace

        std::size_t stream(std::uint64_t offset, std::span<char> out)
        {
            auto locked = cache.lock();
            {
                if (locked->cursor_i == 0 && locked->ksyms.empty())
                    locked->ksyms.reserve(static_cast<std::size_t>(kallsyms_num_syms) * 64);

                lib::buffer<char> buf { KSYM_NAME_LEN };
                auto it = std::back_inserter(locked->ksyms);
                while (!locked->ksyms_done && locked->ksyms.size() < offset + out.size())
                {
                    const auto addr = sym_addr(locked->cursor_i);
                    const char type = expand_symbol(locked->cursor_off, buf.span());

                    fmt::format_to(it, "{:016x} {} {}\n", addr, type, buf.data());

                    locked->cursor_off = next_offset(locked->cursor_off);
                    if (++locked->cursor_i >= kallsyms_num_syms)
                        locked->ksyms_done = true;
                }
            }

            std::size_t produced = 0;
            const auto append = [&](std::uint64_t base, std::string_view src) {
                const auto pos = offset + produced;
                if (pos < base || pos - base >= src.size())
                    return;

                const auto soff = pos - base;
                const auto num = std::min(src.size() - soff, out.size() - produced);
                std::memcpy(out.data() + produced, src.data() + soff, num);
                produced += num;
            };

            append(0, locked->ksyms);
            if (produced < out.size() && locked->ksyms_done)
            {
                if (locked->mod_gen != mod::generation.load(std::memory_order_acquire))
                {
                    locked->modsyms.clear();

                    auto mlocked = mod::modules.read_lock();
                    locked->mod_gen = mod::generation.load(std::memory_order_acquire);

                    auto it = std::back_inserter(locked->modsyms);
                    std::vector<const mod::image_t *> checked;
                    for (const auto &[modname, modent] : *mlocked)
                    {
                        const auto image = modent->image.get();
                        if (image == nullptr || std::ranges::find(checked, image) != checked.end())
                            continue;
                        checked.push_back(image);

                        for (const auto &sym : image->symbols)
                        {
                            fmt::format_to(
                                it, "{:016x} {} {}\t[{}]\n", sym.address, sym.type, sym.name,
                                modname
                            );
                        }
                    }
                }

                append(locked->ksyms.size(), locked->modsyms);
            }
            return produced;
        }

        auto lookup(std::uintptr_t addr, std::span<char> namebuf)
            -> const std::optional<std::uintptr_t>
        {
            if (!in_kernel(addr))
                return std::nullopt;

            std::uintptr_t sym_start = 0, sym_end = 0;
            std::size_t low = 0;

            for (std::size_t high = kallsyms_num_syms, mid; high - low > 1;)
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

            expand_symbol(sym_offset(low), namebuf);
            // std::size_t size = sym_end - sym_start;

            return addr - sym_start;
        }

        std::uintptr_t lookup(std::string_view name)
        {
            std::size_t low = 0, mid;
            std::size_t high = kallsyms_num_syms - 1;

            std::array<char, KSYM_NAME_LEN> namebuf { };

            while (low <= high)
            {
                mid = low + (high - low) / 2;
                expand_symbol(sym_offset(sym_seq(mid)), namebuf);

                const auto ret = name.compare(namebuf.data());
                if (ret > 0)
                    low = mid + 1;
                else if (ret < 0)
                    high = mid - 1;
                else
                    break;
            }

            if (low > high)
                return -1ul;

            low = mid;
            while (low)
            {
                expand_symbol(sym_offset(sym_seq(low - 1)), namebuf);
                if (name.compare(namebuf.data()))
                    break;
                low--;
            }

            return sym_addr(sym_seq(low));
        }
    } // namespace kallsyms

    auto lookup(std::uintptr_t addr, std::span<char> namebuf) -> const std::optional<lookup_result>
    {
        auto search_in = [&](const symbol_table &table) -> std::pair<symbol, std::uintptr_t> {
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
            for (const auto &[name, mod] : mod::modules.read_lock().value())
            {
                if (!mod->image)
                    continue;

                auto [sym, offset] = search_in(mod->image->symbols);
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
        else
            return lookup_result { ret.value(), "kernel" };

        return std::nullopt;
    }

    std::uintptr_t klookup(std::string_view name) { return kallsyms::lookup(name); }

    auto get_symbols(
        const char *strtab, const std::uint8_t *symtab, std::size_t syment, std::size_t symsz,
        std::uintptr_t offset
    ) -> symbol_table
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

            const auto bind = ELF64_ST_BIND(sym->st_info);
            const auto type = ELF64_ST_TYPE(sym->st_info);

            char letter;
            if (sym->st_shndx == SHN_ABS)
                letter = 'a';
            else if (sym->st_shndx == SHN_COMMON)
                letter = 'c';
            else
            {
                switch (type)
                {
                    case STT_FUNC:
                        letter = 't';
                        break;
                    case STT_OBJECT:
                        letter = 'd';
                        break;
                    case STT_SECTION:
                        letter = 'a';
                        break;
                    case STT_FILE:
                        letter = 'a';
                        break;
                    default:
                        letter = '?';
                        break;
                }
            }

            if (bind == STB_WEAK)
                letter = (type == STT_OBJECT) ? 'v' : 'w';

            if (bind != STB_LOCAL && letter >= 'a' && letter <= 'z')
                letter -= 'a' - 'A';

            const std::uintptr_t value = offset + sym->st_value;
            const std::size_t size = sym->st_size;

            symbols.emplace(name, value, size, letter);
        }

        return symbols;
    }

    lib::initgraph::task procfs_register_task {
        "kallsyms.procfs.register", lib::initgraph::postsched_init_engine,
        lib::initgraph::require { fs::procfs::registered_stage() }, [] {
            using namespace ::fs::procfs;
            lib::bug_on(!register_global(
                "kallsyms",
                make_streaming_file_ops([](auto, std::uint64_t offset, std::span<char> out) {
                    return kallsyms::stream(offset, out);
                }),
                node_type::file, 0444
            ));
        }
    };
} // namespace bin::elf::sym
