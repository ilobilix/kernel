// Copyright (C) 2024-2026  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.bin.exec;
import system.random;
import system.memory;
import system.sched;
import system.vfs;
import boot;
import lib;
import std;

namespace bin::elf::exec
{
    namespace
    {
        constexpr std::string_view fmt_name { "elf" };
        constexpr std::uintptr_t default_base = 0x400000;
        constexpr std::uintptr_t default_interp_base = 0x40000000;

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
            std::string execfn;
            std::uintptr_t entry;
            std::uintptr_t interp_base;
            auxval auxv;
        };

        std::optional<Elf64_Ehdr> read_ehdr(const std::shared_ptr<vfs::file_t> &file)
        {
            Elf64_Ehdr ehdr;
            auto hdruspan = lib::maybe_uspan<std::byte>::create(
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            );
            if (!hdruspan.has_value())
                return std::nullopt;

            const auto ret = file->pread(0, std::move(*hdruspan));
            if (!ret.has_value() || *ret != sizeof(ehdr))
                return std::nullopt;

            return ehdr;
        }

        bool is_valid_elf(const Elf64_Ehdr &ehdr)
        {
            return std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) == 0 &&
                ehdr.e_ident[EI_CLASS] == ELFCLASS64 &&
                ehdr.e_ident[EI_DATA] == ELFDATA2LSB &&
                (ehdr.e_ident[EI_OSABI] == ELFOSABI_SYSV ||
                    ehdr.e_ident[EI_OSABI] == ELFOSABI_LINUX) &&
                ehdr.e_ident[EI_VERSION] == EV_CURRENT &&
                ehdr.e_machine == EM_CURRENT;
        }

        auto load_file(
            const std::shared_ptr<vfs::file_t> &file, const Elf64_Ehdr &ehdr,
            std::shared_ptr<vmm::vmspace> &vmspace, std::uintptr_t &addr
        ) -> std::optional<std::tuple<auxval, std::shared_ptr<vfs::file_t>, std::uintptr_t>>
        {
            if (ehdr.e_type != ET_DYN)
                addr = 0;

            const auto psize = vmm::default_page_size();
            const auto npsize = vmm::pagemap::from_page_size(psize);

            std::shared_ptr<vfs::file_t> interp { };

            std::uintptr_t max_end = 0;
            std::uintptr_t base_addr = addr;
            bool is_first = true;

            std::uintptr_t phdr_vaddr = 0;
            bool has_phdr = false;
            std::uintptr_t phdr_load_vaddr = 0;
            bool has_phdr_load = false;

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
                        lib::error_name(ret.error())
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
                        if (phdr.p_memsz == 0)
                            break;

                        if (phdr.p_filesz > phdr.p_memsz)
                        {
                            lib::error("elf: phdr filesz larger than memsz");
                            return std::nullopt;
                        }

                        auto prot = vmm::prot::none;
                        if (phdr.p_flags & PF_R)
                            prot |= vmm::prot::read;
                        if (phdr.p_flags & PF_W)
                            prot |= vmm::prot::write;
                        if (phdr.p_flags & PF_X)
                            prot |= vmm::prot::exec;

                        const auto misalign = phdr.p_vaddr & (npsize - 1);
                        const auto paloffset = phdr.p_offset - misalign;

                        const auto file_span = misalign + phdr.p_filesz;
                        const auto mem_span = lib::align_up(misalign + phdr.p_memsz, npsize);

                        vmm::object::ptr fobj;
                        if (auto oret = file->map(); oret.has_value() && *oret &&
                            (*oret)->type != vmm::object_type::mmio)
                            fobj = std::move(*oret);

                        const auto full_len = fobj ? lib::align_down(file_span, npsize) : 0;

                        const auto seg_vaddr = phdr.p_vaddr - misalign;
                        std::uintptr_t seg_addr = 0;

                        const auto map_part = [&](
                            std::uintptr_t seg_off, std::size_t length,
                            vmm::object::ptr obj, std::uint64_t obj_offset
                        ) {
                            auto flags = vmm::flag::private_;// | vmm::flag::untouchable;
                            std::uintptr_t address = 0;

                            if (ehdr.e_type != ET_DYN)
                            {
                                flags |= vmm::flag::fixed;
                                address = seg_vaddr + seg_off;
                            }
                            else
                            {
                                if (base_addr || !is_first)
                                    flags |= vmm::flag::fixed;

                                address = base_addr ? (base_addr + seg_vaddr + seg_off) : 0;
                            }

                            const auto mret = vmspace->map(
                                address, length, prot, prot, flags,
                                std::move(obj), obj_offset
                            );

                            if (!mret.has_value())
                            {
                                lib::error(
                                    "elf: could not map segment: {}",
                                    lib::error_name(mret.error())
                                );
                                return false;
                            }

                            if (!base_addr && is_first)
                            {
                                base_addr = mret.value() - (seg_vaddr + seg_off);
                                addr = base_addr;
                            }

                            if (seg_off == 0)
                            {
                                seg_addr = base_addr
                                    ? (base_addr + seg_vaddr)
                                    : (mret.value() - seg_off);
                            }

                            is_first = false;
                            return true;
                        };

                        if (full_len > 0 && !map_part(0, full_len, fobj, paloffset))
                            return std::nullopt;

                        if (mem_span > full_len)
                        {
                            vmm::object::ptr tail_obj { new vmm::memobject { } };

                            if (const auto tail_len = file_span - full_len; tail_len > 0)
                            {
                                lib::membuffer tail_buffer { tail_len };
                                const auto tail_uspan = tail_buffer.maybe_uspan();
                                lib::panic_if(!tail_uspan.has_value());

                                const auto ret = file->pread(
                                    paloffset + full_len, tail_uspan.value()
                                );
                                if (!ret.has_value() || *ret != tail_len)
                                {
                                    lib::error("elf: could not read phdr data");
                                    return std::nullopt;
                                }

                                if (tail_obj->write(0, tail_uspan.value()) != tail_len)
                                {
                                    lib::error("elf: could not write data to memobject");
                                    return std::nullopt;
                                }
                            }

                            if (!map_part(full_len, mem_span - full_len, std::move(tail_obj), 0))
                                return std::nullopt;
                        }

                        if (!has_phdr_load)
                        {
                            const auto phdr_end = ehdr.e_phoff +
                                static_cast<std::uint64_t>(ehdr.e_phnum) * ehdr.e_phentsize;
                            if (ehdr.e_phoff >= phdr.p_offset &&
                                phdr_end <= phdr.p_offset + phdr.p_filesz)
                            {
                                phdr_load_vaddr = phdr.p_vaddr + (ehdr.e_phoff - phdr.p_offset);
                                has_phdr_load = true;
                            }
                        }

                        max_end = std::max(max_end, seg_addr + misalign + phdr.p_memsz);
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
                                lib::error_name(ret.error())
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

                        interp = vfs::file_t::create(std::move(*res), 0, 0);
                        break;
                    }
                    default:
                        break;
                }
            }

            const auto at_phdr = has_phdr
                ? (addr + phdr_vaddr)
                : (has_phdr_load ? (addr + phdr_load_vaddr) : (addr + ehdr.e_phoff));

            return std::make_tuple(
                auxval {
                    .at_entry = addr + ehdr.e_entry,
                    .at_phdr = at_phdr,
                    .at_phent = ehdr.e_phentsize,
                    .at_phnum = ehdr.e_phnum
                },
                std::move(interp),
                lib::align_up(max_end, npsize)
            );
        }

        [[noreturn]]
        void trampoline(ctx *ctx)
        {
            std::uintptr_t entry, stack;
            {
                auto &req = ctx->req;
                auto &auxv = ctx->auxv;

                const auto thread = sched::current_thread();
                const auto proc = thread->proc.get();

                const auto stack_size = boot::ustack_size;
                const auto addr_top = thread->ustack_top;
                const auto addr_bottom = addr_top - stack_size;

                const std::string_view execfn_path = ctx->execfn;
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

                const auto envp_top = offset;
                for (const auto &env : req.envp)
                {
                    offset -= env.length() + 1;
                    copy_to_user(curr(), env.c_str(), env.length() + 1);
                }

                const auto argv_top = offset;
                for (const auto &arg : req.argv)
                {
                    offset -= arg.length() + 1;
                    copy_to_user(curr(), arg.c_str(), arg.length() + 1);
                }

                std::uintptr_t execfn_offset = 0;
                {
                    offset -= execfn_path.length() + 1;
                    copy_to_user(curr(), execfn_path.data(), execfn_path.length() + 1);
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
                    lib::bug_on(random::get_bytes(*uspan) != 16);
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
                write_auxv(AT_UID, proc->cred->ruid);
                write_auxv(AT_EUID, proc->cred->euid);
                write_auxv(AT_GID, proc->cred->rgid);
                write_auxv(AT_EGID, proc->cred->egid);
                write_auxv(AT_PLATFORM, platform_offset);
                // write_auxv(AT_HWCAP, 0); // TODO
                write_auxv(AT_EXECFN, execfn_offset);
                write_auxv(AT_RANDOM, random_offset);
                write_auxv(AT_SECURE, 0);
                write_auxv(AT_MINSIGSTKSZ, sched::min_altstack_size());

                offset -= 8;
                write(0);

                offset -= 8 * req.envp.size();
                auto array_base = addr_bottom + offset;
                auto str_offset = envp_top;
                for (std::size_t i = 0; const auto &env : req.envp)
                {
                    str_offset -= env.length() + 1;
                    const auto addr = addr_bottom + str_offset;
                    copy_to_user(array_base + i * 8, &addr, sizeof(addr));
                    i++;
                }

                offset -= 8;
                write(0);

                offset -= 8 * req.argv.size();
                array_base = addr_bottom + offset;
                str_offset = argv_top;
                for (std::size_t i = 0; const auto &arg : req.argv)
                {
                    str_offset -= arg.length() + 1;
                    const auto addr = addr_bottom + str_offset;
                    copy_to_user(array_base + i * 8, &addr, sizeof(addr));
                    i++;
                }

                offset -= 8;
                write(req.argv.size());

                entry = ctx->entry;
                stack = addr_bottom + offset;
                delete ctx;
            }

            sched::jump_to_user(entry, stack);
        }
    } // namespace

    class image : public bin::exec::image
    {
        private:
        Elf64_Ehdr _ehdr;

        public:
        image(std::shared_ptr<vfs::file_t> file, const Elf64_Ehdr &ehdr)
            : bin::exec::image { std::move(file) }, _ehdr { ehdr } { }

        std::shared_ptr<sched::thread_t> load(const bin::exec::request &req) const override
        {
            lib::bug_on(!req.proc);

            std::uintptr_t exec_base = default_base;
            std::uintptr_t interp_base = 0;

            auto ret = load_file(file, _ehdr, req.proc->vmspace, exec_base);
            if (!ret.has_value())
                return nullptr;

            const auto [auxv, interp, exec_end] = ret.value();

            std::uintptr_t brk_base = exec_end;
            std::uintptr_t entry = auxv.at_entry;

            if (interp)
            {
                interp_base = default_interp_base;

                const auto iehdr = read_ehdr(interp);
                if (!iehdr || !is_valid_elf(*iehdr))
                {
                    lib::error("elf: invalid interpreter");
                    return nullptr;
                }

                ret = load_file(interp, *iehdr, req.proc->vmspace, interp_base);
                if (!ret.has_value())
                    return nullptr;

                lib::bug_on(interp_base == exec_base);
                const auto [iauxv, ii, interp_end] = ret.value();

                brk_base = std::max(brk_base, interp_end);

                entry = iauxv.at_entry;
                lib::bug_on(ii != nullptr);
            }

            req.proc->vmspace->brk_start = brk_base;
            req.proc->vmspace->current_brk = brk_base;

            auto execfn = req.pathname.empty()
                ? vfs::pathname_from(file->path)
                : req.pathname;
            const auto arg = new ctx {
                req, std::move(execfn), entry, interp_base, auxv
            };
            return sched::create_uthread(
                req.proc->shared_from_this(),
                reinterpret_cast<std::uintptr_t>(trampoline),
                reinterpret_cast<std::uintptr_t>(arg),
                true, false, 0
            );
        }

        std::string_view format_name() const override { return fmt_name; }
    };

    class format : public bin::exec::format
    {
        public:
        format() : bin::exec::format { fmt_name } { }

        lib::expect<std::unique_ptr<bin::exec::image>> probe(
            const std::shared_ptr<vfs::file_t> &file, std::size_t depth
        ) const override
        {
            lib::unused(depth);
            const auto ehdr = read_ehdr(file);
            if (!ehdr || !is_valid_elf(*ehdr))
                return nullptr;
            return std::make_unique<image>(file, *ehdr);
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
