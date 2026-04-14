// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.bin.exec;
import system.scheduler;
import system.memory;
import system.vfs;
import boot;
import lib;
import cppstd;

namespace bin::elf::exec
{
    class format : public bin::exec::format
    {
        private:
        static inline constexpr std::uintptr_t default_base = 0x400000;

        struct auxval
        {
            Elf64_Addr at_entry;
            Elf64_Addr at_phdr;
            Elf64_Addr at_phent;
            Elf64_Addr at_phnum;
        };

        static auto load_file(const std::shared_ptr<vfs::file> &file, std::shared_ptr<vmm::vmspace> &vmspace, std::uintptr_t addr)
            -> std::optional<std::pair<auxval, std::shared_ptr<vfs::file>>>
        {
            Elf64_Ehdr ehdr;
            lib::panic_if(file->pread(0, std::span {
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            }) != sizeof(ehdr));

            if (ehdr.e_type != ET_DYN)
                addr = 0;

            auxval aux
            {
                .at_entry = addr + ehdr.e_entry,
                .at_phdr = addr + ehdr.e_phoff,
                .at_phent = ehdr.e_phentsize,
                .at_phnum = ehdr.e_phnum
            };

            std::shared_ptr<vfs::file> interp { };

            for (std::size_t i = 0; i < ehdr.e_phnum; i++)
            {
                Elf64_Phdr phdr;
                lib::panic_if(file->pread(
                    ehdr.e_phoff + i * ehdr.e_phentsize,
                    std::span { reinterpret_cast<std::byte *>(&phdr), sizeof(phdr) }
                ) != sizeof(phdr));

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

                        const auto psize = vmm::default_page_size();
                        const auto misalign = phdr.p_vaddr & (psize - 1);

                        const auto address = addr + phdr.p_vaddr - misalign;

                        auto obj = std::make_shared<vmm::memobject>();

                        lib::membuffer file_buffer { phdr.p_filesz };
                        lib::panic_if(file->pread(
                            phdr.p_offset, file_buffer.span()
                        ) != static_cast<std::ssize_t>(phdr.p_filesz));

                        lib::panic_if(obj->write(
                            misalign, file_buffer.span()
                        ) != phdr.p_filesz);

                        if (phdr.p_memsz > phdr.p_filesz)
                        {
                            const auto zeroes_len = phdr.p_memsz - phdr.p_filesz;
                            lib::membuffer zeroes { zeroes_len };
                            std::memset(zeroes.data(), 0, zeroes.size());

                            lib::panic_if(obj->write(
                                misalign + phdr.p_filesz, zeroes.span()
                            ) != zeroes_len);
                        }

                        lib::panic_if(!vmspace->map(
                            address, phdr.p_memsz + misalign,
                            prot, vmm::flag::private_,
                            obj, 0
                        ));

                        break;
                    }
                    case PT_PHDR:
                        aux.at_phdr = addr + phdr.p_vaddr;
                        break;
                    case PT_INTERP:
                    {
                        lib::membuffer buffer { phdr.p_filesz - 1 };
                        lib::panic_if(file->pread(
                            phdr.p_offset, buffer.span()
                        ) != static_cast<std::ssize_t>(phdr.p_filesz - 1));

                        std::string_view path {
                            reinterpret_cast<const char *>(buffer.data()),
                            phdr.p_filesz - 1
                        };

                        auto ret = vfs::resolve(file->path, path);
                        if (!ret.has_value())
                            return std::nullopt;

                        auto res = vfs::reduce(ret->parent, ret->target);
                        if (!res.has_value())
                            return std::nullopt;

                        interp = vfs::file::create(res.value(), 0, 0);
                        break;
                    }
                    default:
                        break;
                }
            }

            return std::make_pair(aux, interp);
        }

        public:
        format() : bin::exec::format { "elf" } { }

        bool identify(const std::shared_ptr<vfs::file> &file) const override
        {
            Elf64_Ehdr ehdr;
            lib::bug_on(file->pread(0, std::span {
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            }) != sizeof(ehdr));

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

            auto ret = load_file(req.file, proc->vmspace, default_base);
            if (!ret.has_value())
                return nullptr;

            const auto auxv = ret->first;

            lib::bug_on(req.interp && ret->second);

            std::uintptr_t entry = ret->first.at_entry;
            if (ret->second)
            {
                ret = load_file(ret->second, proc->vmspace, 0);
                if (!ret.has_value())
                    return nullptr;

                entry = ret->first.at_entry;
                lib::bug_on(ret->second != nullptr);
            }

            auto thread = sched::thread::create(proc, entry, true);

            lib::bug_on(thread->ustack_obj.expired());
            const auto obj = thread->ustack_obj.lock();

            const auto stack_size = boot::ustack_size;
            const auto addr_bottom = thread->ustack_top - stack_size;

            constexpr std::size_t num_auxvals = 6;

            const bool one_more = (req.argv.size() + req.envp.size() + 1) & 1;
            const auto required_size =
                lib::align_up(
                    std::accumulate(
                        req.envp.begin(), req.envp.end(), std::size_t { 0 },
                        [](std::size_t acc, const std::string &env) {
                            return acc + env.length() + 1;
                        }
                    ) +
                    std::accumulate(
                        req.argv.begin(), req.argv.end(), std::size_t { 0 },
                        [](std::size_t acc, const std::string &arg) {
                            return acc + arg.length() + 1;
                        }
                    ), 16
                ) + (one_more ? 8 : 0) + (num_auxvals * 16) + 8 +
                req.envp.size() * 8 + 8 + req.argv.size() * 8 + 8;

            lib::bug_on(required_size % 16 != 0);

            lib::membuffer stack_buffer { required_size };
            std::memset(stack_buffer.data(), 0, stack_buffer.size());

            auto offset = stack_size;
            const auto sptr = [&] {
                return reinterpret_cast<std::uintptr_t *>(
                    reinterpret_cast<std::uintptr_t>(stack_buffer.data()) + required_size - (stack_size - offset)
                );
            };

            std::vector<std::uintptr_t> envp_offsets { };
            envp_offsets.reserve(req.envp.size());
            for (const auto &env : req.envp)
            {
                offset -= env.length() + 1;
                std::memcpy(sptr(), env.c_str(), env.length() + 1);
                envp_offsets.push_back(addr_bottom + offset);
            }

            std::vector<std::uintptr_t> argv_offsets { };
            argv_offsets.reserve(req.argv.size());
            for (const auto &arg : req.argv)
            {
                offset -= arg.length() + 1;
                std::memcpy(sptr(), arg.c_str(), arg.length() + 1);
                argv_offsets.push_back(addr_bottom + offset);
            }

            offset = lib::align_down(offset, 16);
            if (one_more)
            {
                offset -= 8;
                *sptr() = 0;
            }

            std::size_t num = 0;
            const auto write_auxv = [&](int type, std::uint64_t value)
            {
                if (num++ == num_auxvals)
                    lib::panic("you froggot to update num_auxvals");

                offset -= 8;
                *sptr() = value;
                offset -= 8;
                *sptr() = type;
            };

            write_auxv(AT_NULL, 0);
            write_auxv(AT_PHDR, auxv.at_phdr);
            write_auxv(AT_PHENT, auxv.at_phent);
            write_auxv(AT_PHNUM, auxv.at_phnum);
            write_auxv(AT_ENTRY, auxv.at_entry);
            // write_auxv(AT_EXECFN, 0);
            // write_auxv(AT_RANDOM, 0);
            write_auxv(AT_SECURE, 0);

            offset -= 8;
            *sptr() = 0;

            for (auto it = envp_offsets.rbegin(); it != envp_offsets.rend(); it++)
            {
                offset -= 8;
                *sptr() = *it;
            }

            offset -= 8;
            *sptr() = 0;

            for (auto it = argv_offsets.rbegin(); it != argv_offsets.rend(); it++)
            {
                offset -= 8;
                *sptr() = *it;
            }

            offset -= 8;
            *sptr() = req.argv.size();

            lib::bug_on((stack_size - offset) != required_size);

            lib::panic_if(obj->write(
                offset, stack_buffer.span()
            ) != stack_buffer.size());

            thread->update_ustack(addr_bottom + offset);
            return thread;
        }
    };

    lib::initgraph::task elf_exec_task
    {
        "bin.exec.elf.register",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require { lib::initgraph::base_stage() },
        [] { bin::exec::register_format(std::make_shared<format>()); }
    };
} // namespace bin::elf::exec