// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>

module system.bin.elf;

import system.bin.exec;
import system.scheduler;
import system.memory;
import system.vfs;
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

        static auto load_file(const vfs::path &file, std::shared_ptr<vmm::vmspace> &vmspace, std::uintptr_t addr) -> std::optional<std::pair<auxval, std::optional<vfs::path>>>
        {
            lib::bug_on(!file.dentry->inode);
            auto &inode = file.dentry->inode;

            Elf64_Ehdr ehdr;
            lib::panic_if(inode->read(0, std::span {
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            }) != sizeof(ehdr));

            if (ehdr.e_type != ET_DYN)
                addr = 0;

            auxval aux { };
            std::optional<vfs::path> interp { };

            for (std::size_t i = 0; i < ehdr.e_phnum; i++)
            {
                Elf64_Phdr phdr;
                lib::panic_if(inode->read(
                    ehdr.e_phoff + i * sizeof(Elf64_Phdr),
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
                        lib::panic_if(inode->read(
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
                        lib::panic_if(inode->read(
                            phdr.p_offset, buffer.span()
                        ) != static_cast<std::ssize_t>(phdr.p_filesz - 1));

                        std::string_view path {
                            reinterpret_cast<const char *>(buffer.data()),
                            phdr.p_filesz - 1
                        };

                        auto ret = vfs::resolve(file, path);
                        if (!ret.has_value())
                            return std::nullopt;

                        interp = ret->target;
                        break;
                    }
                    default:
                        break;
                }
            }

            aux.at_entry = addr + ehdr.e_entry;
            aux.at_phent = ehdr.e_phentsize;
            aux.at_phnum = ehdr.e_phnum;

            return std::make_pair(aux, interp);
        }

        public:
        format() : bin::exec::format { "elf" } { }

        bool identify(const std::shared_ptr<vfs::dentry> &file) const override
        {
            lib::bug_on(!file || !file->inode);
            auto &inode = file->inode;

            Elf64_Ehdr ehdr;
            lib::bug_on(inode->read(0, std::span {
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

            lib::bug_on(req.interp.has_value() && ret->second.has_value());

            std::uintptr_t entry = ret->first.at_entry;
            if (ret->second.has_value())
            {
                ret = load_file(*ret->second, proc->vmspace, 0);
                if (!ret.has_value())
                    return nullptr;

                entry = ret->first.at_entry;
                lib::bug_on(ret->second.has_value());
            }

            auto thread = sched::thread::create(proc, entry, true);

            const auto addr = thread->modify_ustack();
            auto stack = addr;
            const auto sptr = [&stack] { return reinterpret_cast<std::uintptr_t *>(stack); };

            std::vector<std::uintptr_t> envp_addrs { };
            envp_addrs.reserve(req.envp.size());
            for (const auto &env : req.envp)
            {
                stack -= env.length() + 1;
                std::memcpy(sptr(), env.c_str(), env.length() + 1);
                envp_addrs.push_back(stack);
            }

            std::vector<std::uintptr_t> argv_addrs { };
            argv_addrs.reserve(req.argv.size());
            for (const auto &arg : req.argv)
            {
                stack -= arg.length() + 1;
                std::memcpy(sptr(), arg.c_str(), arg.length() + 1);
                argv_addrs.push_back(stack);
            }

            stack = lib::align_down(stack, 16);
            if ((req.argv.size() + req.envp.size() + 1) & 1)
            {
                stack -= 8;
                *sptr() = 0;
            }

            const auto write_auxv = [&stack, &sptr](int type, std::uint64_t value)
            {
                stack -= 8;
                *sptr() = value;
                stack -= 8;
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

            stack -= 8;
            *sptr() = 0;

            for (auto it = envp_addrs.rbegin(); it != envp_addrs.rend(); it++)
            {
                stack -= 8;
                *sptr() = *it;
            }

            stack -= 8;
            *sptr() = 0;

            for (auto it = argv_addrs.rbegin(); it != argv_addrs.rend(); it++)
            {
                stack -= 8;
                *sptr() = *it;
            }

            stack -= 8;
            *sptr() = req.argv.size();

            lib::bug_on(stack % 16 != 0);

            thread->update_ustack(reinterpret_cast<std::uintptr_t>(stack));
            return thread;
        }
    };

    initgraph::task elf_exec_task
    {
        "exec.register-elf",
        [] { bin::exec::register_format(std::make_shared<format>()); }
    };
} // namespace bin::elf::exec