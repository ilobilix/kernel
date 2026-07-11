// Copyright (C) 2024-2026  ilobilo

module;

#include <elf.h>
#include <version.h>

module system.bin.elf;

import drivers.initramfs;
import system.memory;
import system.vfs;
import system.pci;
import magic_enum;
import fmt;
import lib;
import std;

namespace bin::elf::mod
{
    extern "C" char __start_modules[], __end_modules[];

    void initfini_t::init()
    {
        if (init_array == 0 || init_array_size == 0 || inited)
            return;

        auto arr = reinterpret_cast<void (**)()>(init_array);
        for (std::size_t i = 0; i < init_array_size / sizeof(std::uintptr_t); i++)
        {
            if (auto func = arr[i])
                func();
        }

        inited = true;
    }

    void initfini_t::fini()
    {
        if (fini_array == 0 || fini_array_size == 0 || finied || !inited)
            return;

        auto arr = reinterpret_cast<void (**)()>(fini_array);
        for (std::size_t i = 0; i < fini_array_size / sizeof(std::uintptr_t); i++)
        {
            if (auto func = arr[i])
                func();
        }

        finied = true;
    }

    bool alias_t::match(std::string_view modalias) const
    {
        auto str = modalias.data();
        auto pat = pattern.data();

        const char *sp = nullptr;
        const char *ss = nullptr;

        while (*str)
        {
            if (*str == *pat)
            {
                str++;
                pat++;
            }
            else if (*pat == '*')
            {
                sp = pat;
                ss = str;
                pat++;
            }
            else if (sp)
            {
                pat = sp + 1;
                str = ++ss;
            }
            else return false;
        }

        while (*pat == '*')
            pat++;

        return *pat == '\0';
    }

    namespace
    {
        void unmap_pages(const decltype(image_t::pages) &pages)
        {
            auto &pmap = vmm::kernel_pagemap;
            for (const auto [vaddr, paddr] : pages)
            {
                if (auto ret = pmap->unmap(vaddr, pmm::page_size); !ret)
                {
                    lib::panic(
                        "could not unmap memory for a module: {}",
                        lib::error_name(ret.error())
                    );
                }
                pmm::free(paddr);
            }
        }

        void log_entry(entry_t &entry)
        {
            auto ptr = entry.header;
            const std::string_view desc { ptr->description };
            lib::info("elf: - description: '{}'", desc);
            lib::info("elf: - type: {}", magic_enum::enum_name(ptr->type));

            lib::info("elf: - aliases:");
            for (const auto &[pattern] : entry.aliases)
                lib::info("elf: -  {}", pattern);

            const auto deps = entry.header->dependencies();
            if (deps.empty())
                return;

            lib::info("elf: - dependencies:");
            for (const auto &dep : deps)
                lib::info("elf: -  {}", dep);
        }

        std::size_t load(
            bool internal, std::uintptr_t start, std::uintptr_t end,
            std::shared_ptr<image_t> image
        )
        {
            using base_type = ::mod::declare<0, 0>;

            std::size_t count = 0;
            for (auto current = start; current < end; )
            {
                const auto ptr = reinterpret_cast<base_type *>(current);
                if (ptr->magic != base_type::header_magic)
                {
                    current += 8;
                    continue;
                }

                const std::string_view version = ptr->version;
                if (version != base_type::build_version)
                {
                    lib::error(
                        "elf: incompatible module build version: '{}' (expected '{}')",
                        version, base_type::build_version
                    );
                    lib::bug_on(count != 0);
                    return 0;
                }

                if (!magic_enum::enum_contains(ptr->type))
                {
                    lib::error("elf: unsupported module type: {}", std::to_underlying(ptr->type));
                    current += ptr->struct_size;
                    continue;
                }

                auto locked = modules.write_lock();
                if (locked->contains(ptr->name()))
                {
                    lib::error("elf: module with the name '{}' is already loaded", ptr->name());
                    current += ptr->struct_size;
                    continue;
                }

                lib::info("elf: {}ternal module: '{}'", internal ? "in" : "ex", ptr->name());

                auto entry = std::make_shared<entry_t>();
                entry->internal = internal;
                entry->image = image;
                entry->header = ptr;
                entry->status = status::loaded;
                entry->dependents = 0;

                const auto match = ptr->matches();
                for (std::size_t off = 0; ptr->match_stride != 0 &&
                    off + ptr->match_stride <= match.size(); off += ptr->match_stride)
                {
                    const auto *str = reinterpret_cast<const char *>(match.data() + off);
                    const auto len = std::strnlen(str, ptr->match_stride);
                    entry->aliases.emplace_back(std::string { str, len });
                }

                log_entry(*entry);

                locked.value()[ptr->name()] = std::move(entry);
                generation.fetch_add(1, std::memory_order_release);

                count++;
                current += ptr->struct_size;
            }

            return count;
        }

        std::size_t load(const lib::path &dir, std::shared_ptr<vfs::file_t> file)
        {
            lib::info("elf: loading '{}'", dir / file->path.dentry->name);

            auto &inode = file->path.dentry->inode;
            lib::membuffer buffer { static_cast<std::size_t>(inode->stat.st_size) };
            const auto buffer_uspan = buffer.maybe_uspan();
            lib::bug_on(!buffer_uspan.has_value());

            if (const auto ret = file->pread(0, buffer_uspan.value());
                !ret.has_value() || *ret != static_cast<std::size_t>(inode->stat.st_size))
            {
                lib::error("elf: could not read module file");
                return 0;
            }

            const auto ehdr = reinterpret_cast<Elf64_Ehdr *>(buffer.data());

            if (std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG))
            {
                lib::error("elf: invalid module magic");
                return 0;
            }
            if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
            {
                lib::error("elf: invalid module class");
                return 0;
            }
            if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
            {
                lib::error("elf: invalid module data type");
                return 0;
            }
            if (ehdr->e_ident[EI_OSABI] != ELFOSABI_SYSV)
            {
                lib::error("elf: invalid module os abi");
                return 0;
            }
            if (ehdr->e_type != ET_DYN)
            {
                lib::error("elf: module is not a shared object");
                return 0;
            }

            const std::span<std::uint8_t> phdrs {
                reinterpret_cast<std::uint8_t *>(buffer.data() + ehdr->e_phoff),
                static_cast<std::size_t>(ehdr->e_phnum * ehdr->e_phentsize)
            };

            std::uint64_t max_size = 0;
            for (std::size_t i = 0; i < ehdr->e_phnum; i++)
            {
                auto phdr = reinterpret_cast<Elf64_Phdr *>(phdrs.data() + ehdr->e_phentsize * i);
                if (phdr->p_type != PT_LOAD)
                    continue;

                std::uint64_t seghi = phdr->p_vaddr + phdr->p_memsz;
                seghi = ((seghi - 1) / phdr->p_align + 1) * phdr->p_align;
                if (seghi > max_size)
                    max_size = seghi;
            }

            decltype(image_t::pages) memory;

            const auto loaded_at = vmm::alloc_vspace(max_size);

            const auto flags = vmm::pflag::rwg;
            const auto psize = vmm::page_size::small;
            const auto npsize = vmm::pagemap::from_page_size(psize);
            const auto npages = lib::div_roundup(npsize, pmm::page_size);

            for (std::size_t i = 0; i < max_size; i += npsize)
            {
                const auto paddr = pmm::alloc(npages, true);
                if (auto ret = vmm::kernel_pagemap->map(loaded_at + i, paddr, npsize, flags, psize); !ret)
                {
                    lib::panic(
                        "could not map memory for a module: {}",
                        lib::error_name(ret.error())
                    );
                }

                memory.emplace_back(loaded_at + i, paddr);
            }

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
                auto phdr = reinterpret_cast<Elf64_Phdr *>(phdrs.data() + ehdr->e_phentsize * i);
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
                        std::memcpy(
                            reinterpret_cast<void *>(loaded_at + phdr->p_vaddr),
                            buffer.data() + phdr->p_offset,
                            phdr->p_filesz
                        );
                        break;
                    }
                    case PT_DYNAMIC:
                    {
                        const std::span<Elf64_Dyn> dyntable {
                            reinterpret_cast<Elf64_Dyn *>(buffer.data() + phdr->p_offset),
                            phdr->p_filesz / sizeof(Elf64_Dyn)
                        };

                        for (std::size_t ii = 0; ii < phdr->p_filesz / sizeof(Elf64_Dyn); ii++)
                        {
                            const auto &dyn = dyntable[ii];
                            if (dyn.d_tag == DT_NULL)
                                break;

                            switch (dyn.d_tag)
                            {
                                case DT_PLTRELSZ:
                                    dt_pltrelsz = dyn.d_un.d_val;
                                    break;
                                case DT_STRTAB:
                                    dt_strtab = loaded_at + dyn.d_un.d_ptr;
                                    break;
                                case DT_SYMTAB:
                                    dt_symtab = loaded_at + dyn.d_un.d_ptr;
                                    break;
                                case DT_RELA:
                                    dt_rela = loaded_at + dyn.d_un.d_ptr;
                                    break;
                                case DT_RELASZ:
                                    dt_relasz = dyn.d_un.d_val;
                                    break;
                                case DT_RELAENT:
                                    dt_relaent = dyn.d_un.d_val;
                                    break;
                                // case DT_STRSZ:
                                //     dt_strsz = dyn.d_un.d_val;
                                //     break;
                                case DT_SYMENT:
                                    dt_syment = dyn.d_un.d_val;
                                    break;
                                case DT_JMPREL:
                                    dt_jmprel = loaded_at + dyn.d_un.d_ptr;
                                    break;
                                case DT_INIT_ARRAY:
                                    dt_init_array = loaded_at + dyn.d_un.d_ptr;
                                    break;
                                case DT_FINI_ARRAY:
                                    dt_fini_array = loaded_at + dyn.d_un.d_ptr;
                                    break;
                                case DT_INIT_ARRAYSZ:
                                    dt_init_arraysz = dyn.d_un.d_val;
                                    break;
                                case DT_FINI_ARRAYSZ:
                                    dt_fini_arraysz = dyn.d_un.d_val;
                                    break;
                                default: break;
                            }
                        }
                        lib::panic_if(
                            dt_strtab == 0 || dt_symtab == 0 || /* dt_strsz == 0 || */
                            dt_rela == 0 || dt_relasz == 0 || dt_relaent == 0
                        );
                        break;
                    }
                    case PT_NULL:
                        break;
                    default:
                        lib::warn("elf: ignoring module phdr type 0x{:X}", type);
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
                    case R_X86_64_NONE:
                        break;
                    case R_X86_64_64:
                    case R_X86_64_GLOB_DAT:
                    case R_X86_64_JUMP_SLOT:
                    {
                        const auto sym = reinterpret_cast<Elf64_Sym *>(
                            symtab + ELF64_R_SYM(rel.r_info) * dt_syment
                        );

                        std::uintptr_t resolved = 0;
                        if (sym->st_shndx == 0)
                        {
                            const std::string_view name { strtab + sym->st_name };
                            if (name.empty())
                                break;

                            auto symaddr = sym::klookup(name);
                            if (symaddr == -1ul)
                            {
                                lib::error("elf: symbol '{}' not found", name);
                                return false;
                            }
                            resolved = symaddr;
                        }
                        else resolved = loaded_at + sym->st_value;

                        *reinterpret_cast<std::uint64_t *>(loc) = resolved;
                        break;
                    }
                    case R_X86_64_RELATIVE:
                        *reinterpret_cast<std::uint64_t *>(loc) = loaded_at + rel.r_addend;
                        break;
#elif defined(__aarch64__)
#endif
                    default:
                        // TODO: remove me when aarch64 relocation is implemented!
                        lib::unused(loc);
                        lib::error("elf: unknown module relocation 0x{:X}", type);
                        return false;
                }
                return true;
            };

            for (std::size_t i = 0; i < static_cast<std::size_t>(dt_relasz) / dt_relaent; i++)
            {
                auto &rela = *reinterpret_cast<Elf64_Rela *>(dt_rela + i * dt_relaent);
                if (!reloc(rela))
                {
                    lib::error("elf: module relocation failed");
                    unmap_pages(memory);
                    return 0;
                }
            }

            for (std::size_t i = 0; i < static_cast<std::size_t>(dt_pltrelsz) / dt_relaent; i++)
            {
                auto &rela = *reinterpret_cast<Elf64_Rela *>(dt_jmprel + i * dt_relaent);
                if (!reloc(rela))
                {
                    lib::error("elf: module relocation failed");
                    unmap_pages(memory);
                    return 0;
                }
            }

            const std::size_t symsz = dt_strtab - dt_symtab;
            auto image = std::make_shared<image_t>(
                std::move(memory),
                sym::get_symbols(strtab, symtab, dt_syment, symsz, loaded_at),
                initfini_t {
                    dt_init_array, dt_fini_array,
                    static_cast<std::size_t>(dt_init_arraysz),
                    static_cast<std::size_t>(dt_fini_arraysz)
                }
            );

            for (std::ssize_t i = ehdr->e_phnum - 1; i >= 0; i--)
            {
                auto phdr = reinterpret_cast<Elf64_Phdr *>(phdrs.data() + ehdr->e_phentsize * i);
                if (phdr->p_type != PT_LOAD)
                    continue;

                auto flags = vmm::pflag::global;
                if (phdr->p_flags & PF_R)
                    flags |= vmm::pflag::read;
                if (phdr->p_flags & PF_W)
                    flags |= vmm::pflag::write;
                if (phdr->p_flags & PF_X)
                    flags |= vmm::pflag::exec;

                const auto aligned = lib::align_down(loaded_at + phdr->p_vaddr, pmm::page_size);
                const auto size = lib::align_up(
                    phdr->p_memsz + (loaded_at + phdr->p_vaddr - aligned),
                    pmm::page_size
                );
                const auto psize = vmm::page_size::small;

                if (auto ret = vmm::kernel_pagemap->protect(aligned, size, flags, psize); !ret)
                {
                    lib::panic(
                        "could not change module memory mapping flags: {}",
                        lib::error_name(ret.error())
                    );
                }
            }

            const auto count = load(
                false, modules_start, modules_start + modules_size, std::move(image)
            );
            if (count == 0)
                lib::error("elf: no modules found in file");
            return count;
        }

        std::size_t load_external_from(const lib::path &dir)
        {
            auto ret = vfs::resolve(std::nullopt, dir);
            if (!ret || ret->target.dentry->inode->stat.type() != stat::type::s_ifdir)
            {
                lib::error("elf: directory '{}' not found", dir);
                return 0;
            }

            std::size_t offset = 3;
            std::size_t count = 0;
            while (true)
            {
                auto res = ret->target.mnt->fs.lock()->readdir(ret->target.dentry, offset);
                if (!res || res->empty())
                    break;

                for (const auto &[name, inode, cookie] : *res)
                {
                    offset = cookie + 1;

                    if (!name.ends_with(".ko") || inode->stat.type() != stat::type::s_ifreg)
                        continue;

                    auto res = vfs::resolve(ret->target, name);
                    if (!res)
                        continue;

                    count += load(dir, vfs::file_t::create(res->target, 0, 0));
                }
            }
            return count;
        }

        bool activate(entry_t &entry)
        {
            {
                auto locked = modules.write_lock();
                switch (entry.status)
                {
                    case status::active:
                        return true;
                    case status::failed:
                        return false;
                    case status::activating:
                        lib::error(
                            "elf: module '{}' is already being activated",
                            entry.header->name()
                        );
                        return false;
                    case status::loaded:
                        entry.status = status::activating;
                        break;
                }
            }

            const auto deps = entry.header->dependencies();

            bool success = true;
            for (const auto name : deps)
            {
                std::shared_ptr<entry_t> dep;
                {
                    const auto rlocked = modules.read_lock();
                    if (auto it = rlocked->find(name); it != rlocked->end())
                        dep = it->second;
                }

                if (dep == nullptr)
                {
                    lib::error(
                        "elf: could not find dependency '{}' of module '{}'",
                        name, entry.header->name()
                    );
                    success = false;
                    break;
                }

                if (!activate(*dep))
                {
                    lib::error(
                        "elf: failed to activate dependency '{}' of module '{}'",
                        name, entry.header->name()
                    );
                    success = false;
                    break;
                }
            }

            if (success)
            {
                if (entry.image)
                    entry.image->initfini.init();

                success = entry.header->init ? entry.header->init() : true;
                if (!success)
                    lib::error("elf: failed to activate module '{}'", entry.header->name());
            }

            auto locked = modules.write_lock();
            if (!success)
            {
                entry.status = status::failed;
                return false;
            }

            for (const auto name : deps)
            {
                if (auto it = locked->find(name); it != locked->end())
                    it->second->dependents++;
            }

            entry.status = status::active;
            return true;
        }

        bool deactivate(entry_t &entry)
        {
            {
                const auto locked = modules.write_lock();
                if (entry.status != status::active)
                    return true;
            }

            const bool success = entry.header->fini ? entry.header->fini() : true;

            auto locked = modules.write_lock();
            for (const auto name : entry.header->dependencies())
            {
                if (auto it = locked->find(name); it != locked->end() && it->second->dependents)
                    it->second->dependents--;
            }

            entry.status = status::loaded;
            return success;
        }
    } // namespace

    image_t::~image_t()
    {
        initfini.fini();
        unmap_pages(pages);
    }

    bool unload(std::string_view name)
    {
        std::shared_ptr<entry_t> entry;
        {
            const auto rlocked = modules.read_lock();
            const auto it = rlocked->find(name);
            if (it == rlocked->end())
            {
                lib::error("elf: cannot unload unknown module '{}'", name);
                return false;
            }
            entry = it->second;

            if (entry->internal)
            {
                lib::error("elf: cannot unload internal module '{}'", name);
                return false;
            }
            if (entry->dependents != 0)
            {
                lib::error(
                    "elf: cannot unload module '{}': {} module(s) still depend on it",
                    name, entry->dependents
                );
                return false;
            }
        }

        if (!deactivate(*entry))
            lib::warn("elf: module '{}' reported failure on fini", name);

        modules.write_lock()->erase(name);
        generation.fetch_add(1, std::memory_order_release);

        lib::info("elf: unloaded module '{}'", name);
        return true;
    }

    bool request_alias(std::string_view modalias)
    {
        std::vector<std::shared_ptr<entry_t>> entries;
        for (const auto rlocked = modules.read_lock(); const auto &[_, entry] : *rlocked)
        {
            for (const auto &alias : entry->aliases)
            {
                if (alias.match(modalias))
                    entries.push_back(entry);
            }
        }
        if (entries.empty())
            return false;

        for (const auto &entry : entries)
        {
            if (!activate(*entry))
                return false;
        }
        return true;
    }

    lib::initgraph::stage *modules_loaded_stage()
    {
        static lib::initgraph::stage stage
        {
            "bin.elf.modules-loaded",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    lib::initgraph::task load_task
    {
        "bin.elf.load-modules",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { initramfs::extracted_stage() },
        lib::initgraph::entail { modules_loaded_stage() },
        [] {
            const auto start = reinterpret_cast<std::uintptr_t>(__start_modules);
            const auto end = reinterpret_cast<std::uintptr_t>(__end_modules);

            const auto icount = load(true, start, end, nullptr);
            lib::info(
                "elf: loaded {} internal module{}",
                icount, icount == 1 ? "" : "s"
            );

            const auto ecount = load_external_from("/usr/lib/modules/" ILOBILIX_RELEASE);
            lib::info(
                "elf: loaded {} external module{}",
                ecount, ecount == 1 ? "" : "s"
            );
        }
    };

    lib::initgraph::task activate_task
    {
        "bin.elf.activate-modules",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require {
            modules_loaded_stage(),
            pci::enumerated_stage()
        },
        [] {
            std::vector<std::shared_ptr<entry_t>> entries;
            for (const auto rlocked = modules.read_lock(); const auto &[name, entry] : *rlocked)
            {
                if (entry->header->type == ::mod::type::generic &&
                    entry->status == status::loaded)
                    entries.push_back(entry);
            }

            for (const auto &entry : entries)
                activate(*entry);
        }
    };
} // namespace bin::elf::mod
