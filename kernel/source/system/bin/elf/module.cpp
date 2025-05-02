// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.memory;
import system.vfs;
import magic_enum;
import lib;
import cppstd;

namespace bin::elf::mod
{
    extern "C" char __section_modules_start[];
    extern "C" char __section_modules_end[];

    void initfini::init()
    {
        if (init_array == 0 || init_array_size == 0)
            return;

        auto arr = reinterpret_cast<void (**)()>(init_array);
        for (std::size_t i = 0; i < init_array_size / sizeof(std::uintptr_t); i++)
        {
            if (auto func = arr[i])
                func();
        }
    }

    void initfini::fini()
    {
        if (fini_array == 0 || fini_array_size == 0)
            return;

        auto arr = reinterpret_cast<void (**)()>(fini_array);
        for (std::size_t i = 0; i < fini_array_size / sizeof(std::uintptr_t); i++)
        {
            if (auto func = arr[i])
                func();
        }
    }

    namespace
    {
        lib::mutex lock;

        void log_entry(auto &entry)
        {
            auto ptr = entry.header;
            log::info("elf: {}ternal module: '{}'", entry.internal ? "in" : "ex", ptr->name);
            log::info("elf: - description: '{}'", ptr->description);

            std::visit(
                lib::overloaded {
                    [&](const ::mod::generic &) { log::info("elf: - type: generic"); },
                    [&](const ::mod::pci &) { log::info("elf: - type: pci"); },
                    [&](const ::mod::acpi &) { log::info("elf: - type: acpi"); }
                }, ptr->interface
            );

            const auto ndeps = entry.deps.size();
            log::info("elf: - dependencies: {}", ndeps);

            if (ndeps != 0)
                log::print(log::level::info, "elf: -  ");
            for (std::size_t i = 0; i < ndeps; i++)
            {
                auto mod = entry.deps[i];
                log::print("'{}'{}", mod, i == ndeps - 1 ? "" : ", ");
            }
            if (ndeps != 0)
                log::println();
        }

        std::size_t load(bool internal, std::uintptr_t start, std::uintptr_t end, decltype(entry::pages) &&pages, sym::symbol_table &&symbols, initfini initfini)
        {
            using base_type = ::mod::declare<0>;
            constexpr std::size_t base_size = sizeof(base_type);

            std::size_t nmod = 0;

            auto current = start;
            while (current < end)
            {
                const auto magic = *reinterpret_cast<std::uint64_t *>(current);
                if (magic != base_type::header_magic)
                {
                    current += 8;
                    continue;
                }

                nmod++;

                const auto ptr = reinterpret_cast<base_type *>(current);
                const auto ndeps = ptr->dependencies.ndeps;
                const auto deps = reinterpret_cast<const char *const *>(ptr->dependencies.list);

                auto &entry = modules[ptr->name];
                entry.internal = internal;
                entry.header = ptr;

                entry.pages = std::move(pages);
                entry.symbols = std::move(symbols);

                entry.initfini = initfini;

                for (std::size_t i = 0; i < ndeps; i++)
                    entry.deps.push_back(deps[i]);

                log_entry(entry);

                current += base_size + ndeps * sizeof(const char *);
            }

            return nmod;
        }

        void load_internal()
        {
            const auto start = reinterpret_cast<std::uintptr_t>(__section_modules_start);
            const auto end = reinterpret_cast<std::uintptr_t>(__section_modules_end);

            auto nmod = load(true, start, end, { }, { }, { });
            log::info("elf: module: found {} internal module{}", nmod, nmod == 1 ? "" : "s");
        }

        bool load(std::shared_ptr<vfs::node> node)
        {
            const std::unique_lock _ { lock };

            log::info("elf: module: loading '{}'", node->name);
            auto &back = node->backing;

            const auto ehdr = std::make_unique<Elf64_Ehdr>();
            lib::ensure(back->read(0, std::span {
                reinterpret_cast<std::byte *>(ehdr.get()),
                sizeof(Elf64_Ehdr)
            }) == sizeof(Elf64_Ehdr));

            if (std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG))
            {
                log::error("elf: module: invalid magic");
                return false;
            }
            if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
            {
                log::error("elf: module: invalid class");
                return false;
            }
            if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
            {
                log::error("elf: module: invalid data type");
                return false;
            }
            if (ehdr->e_ident[EI_OSABI] != ELFOSABI_SYSV)
            {
                log::error("elf: module: invalid os abi");
                return false;
            }
            if (ehdr->e_type != ET_DYN)
            {
                log::error("elf: module: not a shared object");
                return false;
            }

            const auto phdrs = std::make_unique<std::uint8_t[]>(ehdr->e_phnum * ehdr->e_phentsize);
            lib::ensure(back->read(ehdr->e_phoff, std::span {
                reinterpret_cast<std::byte *>(phdrs.get()),
                static_cast<std::size_t>(ehdr->e_phnum * ehdr->e_phentsize)
            }) == ehdr->e_phnum * ehdr->e_phentsize);

            std::uint64_t max_size = 0;
            for (std::size_t i = 0; i < ehdr->e_phnum; i++)
            {
                auto phdr = reinterpret_cast<Elf64_Phdr *>(phdrs.get() + ehdr->e_phentsize * i);
                if (phdr->p_type != PT_LOAD)
                    continue;

                std::uint64_t seghi = phdr->p_vaddr + phdr->p_memsz;
                seghi = ((seghi - 1) / phdr->p_align + 1) * phdr->p_align;
                if (seghi > max_size)
                    max_size = seghi;
            }

            decltype(entry::pages) memory;

            const auto loaded_at = vmm::alloc_vpages(vmm::space_type::modules, max_size);

            const auto flags = vmm::flag::rw;
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto npages = lib::div_roundup(npsize, pmm::page_size);

            for (std::size_t i = 0; i < max_size; i += npsize)
            {
                const auto paddr = pmm::alloc<std::uintptr_t>(npages, true);
                if (auto ret = vmm::kernel_pagemap->map(loaded_at + i, paddr, npsize, flags, psize); !ret)
                    lib::panic("could not map memory for a module: {}", magic_enum::enum_name(ret.error()));

                memory.emplace_back(loaded_at + i, paddr);
            }

            auto unmap_all = [&memory] mutable
            {
                auto &pmap = vmm::kernel_pagemap;
                for (auto [vaddr, paddr] : memory)
                {
                    if (auto ret = pmap->unmap(vaddr, pmm::page_size); !ret)
                        lib::panic("could not unmap memory for a module: {}", magic_enum::enum_name(ret.error()));
                    pmm::free(paddr);
                }
                memory.clear();
            };

            std::uintptr_t modules_start = 0;
            std::size_t modules_size = 0;

            std::ptrdiff_t dt_pltrelsz = 0;
            std::uintptr_t dt_strtab = 0;
            std::uintptr_t dt_symtab = 0;
            std::uintptr_t dt_rela = 0;
            std::ptrdiff_t dt_relasz = 0;
            std::ptrdiff_t dt_relaent = 0;
            // std::ptrdiff_t dt_strsz = 0;
            std::ptrdiff_t dt_syment = 0;
            std::uintptr_t dt_jmprel = 0;

            std::uintptr_t dt_init_array = 0;
            std::uintptr_t dt_fini_array = 0;
            std::ptrdiff_t dt_init_arraysz = 0;
            std::ptrdiff_t dt_fini_arraysz = 0;

            for (std::size_t i = 0; i < ehdr->e_phnum; i++)
            {
                auto phdr = reinterpret_cast<Elf64_Phdr *>(phdrs.get() + ehdr->e_phentsize * i);
                switch (const auto type = phdr->p_type)
                {
                    // .modules
                    case DT_LOOS + 1:
                        modules_start = loaded_at + phdr->p_vaddr;
                        modules_size = phdr->p_filesz;
                        [[fallthrough]];
                    case PT_LOAD:
                    {
                        std::memset(
                            reinterpret_cast<void *>(loaded_at + phdr->p_vaddr + phdr->p_filesz),
                            0, phdr->p_memsz - phdr->p_filesz
                        );
                        lib::ensure(back->read(phdr->p_offset, std::span {
                            reinterpret_cast<std::byte *>(loaded_at + phdr->p_vaddr),
                            phdr->p_filesz
                        }) == static_cast<std::ssize_t>(phdr->p_filesz));
                        break;
                    }
                    case PT_DYNAMIC:
                    {
                        const auto dyntable = std::make_unique<Elf64_Dyn[]>(phdr->p_filesz / sizeof(Elf64_Dyn));
                        lib::ensure(back->read(phdr->p_offset, std::span {
                            reinterpret_cast<std::byte *>(dyntable.get()),
                            phdr->p_filesz
                        }) == static_cast<std::ssize_t>(phdr->p_filesz));

                        for (std::size_t ii = 0; ii < phdr->p_filesz / sizeof(Elf64_Dyn); ii++)
                        {
                            const auto &dyn = dyntable[ii];
                            if (dyn.d_tag == DT_NULL)
                                break;

                            switch (dyn.d_tag)
                            {
                                case DT_PLTRELSZ: dt_pltrelsz = dyn.d_un.d_val; break;
                                case DT_STRTAB: dt_strtab = loaded_at + dyn.d_un.d_ptr; break;
                                case DT_SYMTAB: dt_symtab = loaded_at + dyn.d_un.d_ptr; break;
                                case DT_RELA: dt_rela = loaded_at + dyn.d_un.d_ptr; break;
                                case DT_RELASZ: dt_relasz = dyn.d_un.d_val; break;
                                case DT_RELAENT: dt_relaent = dyn.d_un.d_val; break;
                                // case DT_STRSZ: dt_strsz = dyn.d_un.d_val; break;
                                case DT_SYMENT: dt_syment = dyn.d_un.d_val; break;
                                case DT_JMPREL: dt_jmprel = loaded_at + dyn.d_un.d_ptr; break;
                                case DT_INIT_ARRAY: dt_init_array = loaded_at + dyn.d_un.d_ptr; break;
                                case DT_FINI_ARRAY: dt_fini_array = loaded_at + dyn.d_un.d_ptr; break;
                                case DT_INIT_ARRAYSZ: dt_init_arraysz = dyn.d_un.d_val; break;
                                case DT_FINI_ARRAYSZ: dt_fini_arraysz = dyn.d_un.d_val; break;
                                default: break;
                            }
                        }
                        lib::ensure(
                            dt_strtab != 0 && dt_symtab != 0 && /* dt_strsz != 0 && */
                            dt_rela != 0 && dt_relasz != 0 && dt_relaent != 0
                        );
                        break;
                    }
                    case PT_NULL:
                        break;
                    default:
                        log::warn("elf: module: ignoring phdr type 0x{:X}", type);
                        break;
                }
            }

            const auto strtab = reinterpret_cast<const char *>(dt_strtab);
            const auto symtab = reinterpret_cast<std::uint8_t *>(dt_symtab);

            auto reloc = [&](Elf64_Rela &rel) -> bool
            {
                const std::uintptr_t loc = loaded_at + rel.r_offset;
                switch (auto type = ELF64_R_TYPE(rel.r_info))
                {
#if defined(__x86_64__)
                    case R_X86_64_64:
                    case R_X86_64_GLOB_DAT:
                    case R_X86_64_JUMP_SLOT:
                    {
                        const auto sym = reinterpret_cast<Elf64_Sym *>(symtab + ELF64_R_SYM(rel.r_info) * dt_syment);

                        std::uintptr_t resolved = 0;
                        if (sym->st_shndx == 0)
                        {
                            const std::string_view name { strtab + sym->st_name };
                            if (name.empty())
                                break;

                            auto symbol = sym::klookup(name);
                            if (symbol == sym::empty)
                            {
                                log::error("elf: module: symbol '{}' not found", name);
                                return false;
                            }
                            resolved = symbol.address;
                        }
                        else resolved = loaded_at + sym->st_value;

                        *reinterpret_cast<std::uint64_t *>(loc) = resolved;// + rel.r_addend;
                        break;
                    }
                    case R_X86_64_RELATIVE:
                        *reinterpret_cast<std::uint64_t *>(loc) = loaded_at + rel.r_addend;
                        break;
#elif defined(__aarch64__)
#endif
                    default:
                        log::error("elf: module: unknown relocation 0x{:X}", type);
                        return false;
                }
                return true;
            };

            for (std::size_t i = 0; i < static_cast<std::size_t>(dt_relasz) / dt_relaent; i++)
            {
                auto &rela = *reinterpret_cast<Elf64_Rela *>(dt_rela + i * dt_relaent);
                if (reloc(rela) == false)
                {
                    log::error("elf: module: relocation failed");
                    unmap_all();
                    return false;
                }
            }

            for (std::size_t i = 0; i < static_cast<std::size_t>(dt_pltrelsz) / dt_relaent; i++)
            {
                auto &rela = *reinterpret_cast<Elf64_Rela *>(dt_jmprel + i * dt_relaent);
                if (reloc(rela) == false)
                {
                    log::error("elf: module: relocation failed");
                    unmap_all();
                    return false;
                }
            }

            const std::size_t symsz = dt_strtab - dt_symtab;
            auto symbols = sym::get_symbols(strtab, symtab, dt_syment, symsz, loaded_at);

            for (std::ssize_t i = ehdr->e_phnum - 1; i >= 0; i--)
            {
                auto phdr = reinterpret_cast<Elf64_Phdr *>(phdrs.get() + ehdr->e_phentsize * i);
                if (phdr->p_type != PT_LOAD)
                    continue;

                auto flags = vmm::flag::none;
                if (phdr->p_flags & PF_R)
                    flags |= vmm::flag::read;
                if (phdr->p_flags & PF_W)
                    flags |= vmm::flag::write;
                if (phdr->p_flags & PF_X)
                    flags |= vmm::flag::exec;

                const auto aligned = lib::align_down(loaded_at + phdr->p_vaddr, pmm::page_size);
                const auto size = lib::align_up(phdr->p_memsz + (loaded_at + phdr->p_vaddr - aligned), pmm::page_size);

                if (auto ret = vmm::kernel_pagemap->protect(aligned, size, flags, vmm::page_size::small); !ret)
                    lib::panic("could not change module memory mapping flags: {}", magic_enum::enum_name(ret.error()));
            }

            const auto nmod = load(
                false, modules_start, modules_start + modules_size,
                std::move(memory), std::move(symbols),
                {
                    dt_init_array, dt_fini_array,
                    static_cast<std::size_t>(dt_init_arraysz),
                    static_cast<std::size_t>(dt_fini_arraysz)
                }
            );
            if (nmod == 0)
            {
                log::error("elf: module: no drivers found");
                unmap_all();
                return false;
            }

            return true;
        }

        void load_external()
        {
            auto ret = vfs::resolve(nullptr, "/usr/lib/modules");
            if (!ret || ret->target->backing->stat.type() != stat::type::s_ifdir)
            {
                log::error("elf: module: directory '/usr/lib/modules' not found");
                return;
            }

            auto dir = ret->target;
            dir->fs->populate(dir);

            for (auto &[name, child] : dir->children)
            {
                if (!name.ends_with(".ko") || child->backing->stat.type() != stat::type::s_ifreg)
                    continue;

                load(child);
            }
        }
    } // namespace

    void load()
    {
        load_internal();
        load_external();
    }
} // namespace bin::elf::mod