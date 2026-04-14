// Copyright (C) 2024-2026  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.bin.exec;
import system.scheduler;
import system.memory;
import system.vfs;
import magic_enum;
import boot;
import lib;
import std;

namespace bin::elf::exec
{
    class format : public bin::exec::format
    {
        private:
        static constexpr std::uintptr_t default_base = 0x400000;
        static constexpr std::uintptr_t default_interp_base = 0x40000000;

        struct auxval
        {
            Elf64_Addr at_entry;
            Elf64_Addr at_phdr;
            Elf64_Addr at_phent;
            Elf64_Addr at_phnum;
        };

        struct ctx
        {
            bin::exec::request req;
            std::uintptr_t entry;
            std::uintptr_t interp_base;
            auxval auxv;
        };

        static auto load_file(const std::shared_ptr<vfs::file> &file, std::shared_ptr<vmm::vmspace> &vmspace, std::uintptr_t &addr)
            -> std::optional<std::tuple<auxval, std::shared_ptr<vfs::file>, std::uintptr_t>>
        {
            Elf64_Ehdr ehdr;
            auto hdruspan = lib::maybe_uspan<std::byte>::create(
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            );
            lib::bug_on(!hdruspan.has_value());

            const auto ret = file->pread(0, std::move(*hdruspan));
            if (!ret.has_value())
            {
                lib::error(
                    "elf: could not read header: {}",
                    magic_enum::enum_name(ret.error())
                );
                return std::nullopt;
            }
            if (*ret != sizeof(ehdr))
            {
                lib::error("elf: header size mismatch: {} != {}", *ret, sizeof(ehdr));
                return std::nullopt;
            }

            if (ehdr.e_type != ET_DYN)
                addr = 0;

            const auto psize = vmm::default_page_size();
            const auto npsize = vmm::pagemap::from_page_size(psize);

            std::shared_ptr<vfs::file> interp { };

            std::uintptr_t max_end = 0;
            std::uintptr_t base_addr = addr;
            bool is_first = true;

            std::uintptr_t phdr_vaddr = 0;
            bool has_phdr = false;

            for (std::size_t i = 0; i < ehdr.e_phnum; i++)
            {
                Elf64_Phdr phdr;
                auto phdruspan = lib::maybe_uspan<std::byte>::create(
                    reinterpret_cast<std::byte *>(&phdr), sizeof(phdr)
                );
                lib::bug_on(!phdruspan.has_value());

                const auto ret = file->pread(
                    ehdr.e_phoff + i * ehdr.e_phentsize,
                    std::move(*phdruspan)
                );

                if (!ret.has_value())
                {
                    lib::error(
                        "elf: could not read phdr: {}",
                        magic_enum::enum_name(ret.error())
                    );
                    return std::nullopt;
                }
                if (*ret != sizeof(phdr))
                {
                    lib::error("elf: phdr size mismatch: {} != {}", *ret, sizeof(phdr));
                    return std::nullopt;
                }

                switch (phdr.p_type)
                {
                    case PT_LOAD:
                    {
                        auto prot = vmm::prot::none;
                        if (phdr.p_flags & PF_R)
                            prot |= vmm::prot::read;
                        if (phdr.p_flags & PF_W)
                            prot |= vmm::prot::write;
                        if (phdr.p_flags & PF_X)
                            prot |= vmm::prot::exec;

                        const auto misalign = phdr.p_vaddr & (npsize - 1);

                        const auto paloffset = phdr.p_offset - misalign;
                        const auto padded = phdr.p_filesz + misalign;

                        // TODO: map the file
                        vmm::object::ptr obj { new vmm::memobject { } };

                        lib::membuffer file_buffer { padded };
                        const auto file_uspan = file_buffer.maybe_uspan();
                        lib::panic_if(!file_uspan.has_value());

                        const auto ret = file->pread(paloffset, file_uspan.value());
                        if (!ret.has_value())
                        {
                            lib::error(
                                "elf: could not read phdr data: {}",
                                magic_enum::enum_name(ret.error())
                            );
                            return std::nullopt;
                        }

                        if (*ret != file_uspan->size_bytes())
                        {
                            lib::error(
                                "elf: phdr data size mismatch: {} != {}",
                                *ret, file_uspan->size_bytes()
                            );
                            return std::nullopt;
                        }

                        if (obj->write(0, file_uspan.value()) != padded)
                        {
                            lib::error("elf: could not write data to memobject");
                            return std::nullopt;
                        }

                        if (phdr.p_memsz > phdr.p_filesz)
                        {
                            const auto zeroes_len = phdr.p_memsz - phdr.p_filesz;
                            lib::membuffer zeroes { zeroes_len };
                            std::memset(zeroes.data(), 0, zeroes.size());

                            const auto zeroes_uspan = zeroes.maybe_uspan();
                            lib::bug_on(!zeroes_uspan.has_value());
                            if (obj->write(padded, *zeroes_uspan) != zeroes_len)
                            {
                                lib::error("elf: could not zero out bss");
                                return std::nullopt;
                            }
                        }

                        auto flags = vmm::flag::private_;// | vmm::flag::untouchable;
                        std::uintptr_t address = 0;

                        if (ehdr.e_type != ET_DYN)
                        {
                            flags |= vmm::flag::fixed;
                            address = phdr.p_vaddr - misalign;
                        }
                        else
                        {
                            if (base_addr || !is_first)
                                flags |= vmm::flag::fixed;

                            address = base_addr ? (base_addr + phdr.p_vaddr - misalign) : 0;
                        }

                        const auto mret = vmspace->map(
                            address, phdr.p_memsz + misalign,
                            prot, prot, flags, obj, 0
                        );

                        if (!mret.has_value())
                        {
                            lib::error(
                                "elf: could not map segment: {}",
                                magic_enum::enum_name(mret.error())
                            );
                            return std::nullopt;
                        };

                        if (!base_addr && is_first)
                        {
                            base_addr = mret.value() - (phdr.p_vaddr - misalign);
                            addr = base_addr;
                        }

                        max_end = std::max(max_end, *mret + phdr.p_memsz + misalign);
                        is_first = false;
                        break;
                    }
                    case PT_PHDR:
                        phdr_vaddr = phdr.p_vaddr;
                        has_phdr = true;
                        break;
                    case PT_INTERP:
                    {
                        lib::membuffer buffer { phdr.p_filesz - 1 };
                        const auto buffer_uspan = buffer.maybe_uspan();
                        lib::panic_if(!buffer_uspan.has_value());

                        const auto ret = file->pread(phdr.p_offset, buffer_uspan.value());
                        if (!ret.has_value())
                        {
                            lib::error(
                                "elf: could not read interpreter path: {}",
                                magic_enum::enum_name(ret.error())
                            );
                            return std::nullopt;
                        }

                        if (*ret != phdr.p_filesz - 1)
                        {
                            lib::error(
                                "elf: interpreter path size mismatch: {} != {}",
                                *ret, phdr.p_filesz - 1
                            );
                            return std::nullopt;
                        }

                        std::string_view path {
                            reinterpret_cast<const char *>(buffer.data()),
                            phdr.p_filesz - 1
                        };

                        if (lib::path_view { path } .is_absolute() == false)
                        {
                            lib::error("elf: invalid interpreter path '{}'", path);
                            return std::nullopt;
                        }

                        auto rret = vfs::resolve(file->path, path);
                        if (!rret.has_value())
                        {
                            lib::error("elf: could not resolve interpreter path '{}'", path);
                            return std::nullopt;
                        }

                        auto res = vfs::reduce(std::move(rret->parent), std::move(rret->target));
                        if (!res.has_value())
                        {
                            lib::error("elf: could not reduce interpreter path '{}'", path);
                            return std::nullopt;
                        }

                        interp = vfs::file::create(std::move(*res), 0, 0, 0);
                        break;
                    }
                    default:
                        break;
                }
            }

            auxval aux
            {
                .at_entry = addr + ehdr.e_entry,
                .at_phdr = has_phdr ? (addr + phdr_vaddr) : (addr + ehdr.e_phoff),
                .at_phent = ehdr.e_phentsize,
                .at_phnum = ehdr.e_phnum
            };

            return std::make_tuple(
                aux, std::move(interp),
                lib::align_up(max_end, npsize)
            );
        }

        [[noreturn]]
        static void trampoline(ctx *ctx)
        {
            auto &req = ctx->req;
            auto &auxv = ctx->auxv;

            const auto thread = sched::this_thread();
            const auto proc = thread->parent;

            const auto stack_size = boot::ustack_size;
            const auto addr_top = thread->ustack_top;
            const auto addr_bottom = addr_top - stack_size;

            const auto execfn_path = req.pathname.empty()
                ? vfs::pathname_from(req.file->path)
                : req.pathname;

            const std::string_view plaform_name { ILOBILIX_SYSNAME };

            auto offset = stack_size;
            const auto curr = [&] {
                return addr_bottom + offset;
            };

            const auto copy_to_user = [](std::uintptr_t dest, const void *src, std::size_t len) {
                auto ptr = reinterpret_cast<__user void *>(dest);
                // TODO
                lib::panic_if(!lib::copy_to_user(ptr, src, len));
            };

            const auto write = [&](std::uint64_t val) {
                copy_to_user(curr(), &val, sizeof(val));
            };

            std::vector<std::uintptr_t> envp_offsets { };
            envp_offsets.reserve(req.envp.size());
            for (const auto &env : req.envp)
            {
                offset -= env.length() + 1;
                copy_to_user(curr(), env.c_str(), env.length() + 1);
                envp_offsets.push_back(addr_bottom + offset);
            }

            std::vector<std::uintptr_t> argv_offsets { };
            argv_offsets.reserve(req.argv.size());
            for (const auto &arg : req.argv)
            {
                offset -= arg.length() + 1;
                copy_to_user(curr(), arg.c_str(), arg.length() + 1);
                argv_offsets.push_back(addr_bottom + offset);
            }

            std::uintptr_t execfn_offset = 0;
            {
                offset -= execfn_path.length() + 1;
                copy_to_user(curr(), execfn_path.c_str(), execfn_path.length() + 1);
                execfn_offset = addr_bottom + offset;
            }

            std::uintptr_t platform_offset = 0;
            {
                offset -= plaform_name.length() + 1;
                copy_to_user(curr(), plaform_name.data(), plaform_name.length() + 1);
                platform_offset = addr_bottom + offset;
            }

            std::uintptr_t random_offset = 0;
            {
                offset -= 16;
                const auto uspan = lib::maybe_uspan<std::byte>::create(
                    reinterpret_cast<__user std::byte *>(curr()), 16
                );
                lib::bug_on(!uspan.has_value());
                lib::bug_on(lib::random_bytes(*uspan) != 16);
                random_offset = addr_bottom + offset;
            }

            offset = lib::align_down(offset, 16);
            if ((req.argv.size() + req.envp.size() + 1) & 1)
            {
                offset -= 8;
                write(0);
            }

            const auto write_auxv = [&](int type, std::uint64_t value)
            {
                offset -= 8;
                write(value);
                offset -= 8;
                write(type);
            };

            const auto psize = vmm::default_page_size();
            const auto npsize = vmm::pagemap::from_page_size(psize);

            write_auxv(AT_NULL, 0);
            write_auxv(AT_PHDR, auxv.at_phdr);
            write_auxv(AT_PHENT, auxv.at_phent);
            write_auxv(AT_PHNUM, auxv.at_phnum);
            write_auxv(AT_PAGESZ, npsize);
            write_auxv(AT_BASE, ctx->interp_base);
            write_auxv(AT_ENTRY, auxv.at_entry);
            write_auxv(AT_NOTELF, 0);
            write_auxv(AT_UID, proc->ruid);
            write_auxv(AT_EUID, proc->euid);
            write_auxv(AT_GID, proc->rgid);
            write_auxv(AT_EGID, proc->egid);
            write_auxv(AT_PLATFORM, platform_offset);
            // write_auxv(AT_HWCAP, 0); // TODO
            write_auxv(AT_EXECFN, execfn_offset);
            write_auxv(AT_RANDOM, random_offset);
            write_auxv(AT_SECURE, 0);

            offset -= 8;
            write(0);

            for (auto it = envp_offsets.rbegin(); it != envp_offsets.rend(); it++)
            {
                offset -= 8;
                write(*it);
            }

            offset -= 8;
            write(0);

            for (auto it = argv_offsets.rbegin(); it != argv_offsets.rend(); it++)
            {
                offset -= 8;
                write(*it);
            }

            offset -= 8;
            write(req.argv.size());

            const auto entry = ctx->entry;
            delete ctx;
            thread->enter_user(entry, addr_bottom + offset);
        }

        public:
        format() : bin::exec::format { "elf" } { }

        bool identify(const std::shared_ptr<vfs::file> &file) const override
        {
            Elf64_Ehdr ehdr;
            auto ehdruspan = lib::maybe_uspan<std::byte>::create(
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            );
            if (!ehdruspan.has_value())
                return false;

            const auto ret = file->pread(0, std::move(*ehdruspan));
            if (!ret.has_value())
                return false;
            if (*ret != sizeof(ehdr))
                return false;

            return std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) == 0 &&
                ehdr.e_ident[EI_CLASS] == ELFCLASS64 &&
                ehdr.e_ident[EI_DATA] == ELFDATA2LSB &&
                ehdr.e_ident[EI_OSABI] == ELFOSABI_SYSV &&
                ehdr.e_ident[EI_VERSION] == EV_CURRENT &&
                ehdr.e_machine == EM_CURRENT;
        }

        sched::thread *load(const bin::exec::request &req,  sched::process *proc) const override
        {
            lib::bug_on(!proc);

            std::uintptr_t exec_base = default_base;
            std::uintptr_t interp_base = 0;

            auto ret = load_file(req.file, proc->vmspace, exec_base);
            if (!ret.has_value())
                return nullptr;

            const auto [auxv, interp, exec_end] = ret.value();
            lib::bug_on(req.interp && interp);

            std::uintptr_t entry = auxv.at_entry;
            if (interp)
            {
                interp_base = default_interp_base;
                ret = load_file(interp, proc->vmspace, interp_base);
                if (!ret.has_value())
                    return nullptr;

                lib::bug_on(interp_base == exec_base);
                const auto [iauxv, ii, interp_end] = ret.value();

                entry = iauxv.at_entry;
                lib::bug_on(ii != nullptr);
            }

            proc->vmspace->current_brk = exec_end;

            const auto arg = new ctx { req, entry, interp_base, auxv };
            return sched::thread::create(
                proc,
                reinterpret_cast<std::uintptr_t>(trampoline),
                reinterpret_cast<std::uintptr_t>(arg),
                true
            );
        }
    };

    lib::initgraph::task elf_exec_task
    {
        "bin.exec.elf.register",
        lib::initgraph::postsched_init_engine,
        [] {
            bin::exec::register_format(std::make_shared<format>());
        }
    };
} // namespace bin::elf::exec
