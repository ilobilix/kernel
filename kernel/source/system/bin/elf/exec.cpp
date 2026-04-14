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
            lib::bug_on(inode->read(0, std::span {
                reinterpret_cast<std::byte *>(&ehdr), sizeof(ehdr)
            }) != sizeof(ehdr));

            if (ehdr.e_type != ET_DYN)
                addr = 0;

            auxval aux { };
            std::optional<vfs::path> interp { };

            for (std::size_t i = 0; i < ehdr.e_phnum; i++)
            {
                Elf64_Phdr phdr;
                lib::bug_on(inode->read(
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
                        const auto fsize = lib::align_up(phdr.p_filesz + misalign, psize);
                        const auto msize = lib::align_up(phdr.p_memsz + misalign, psize);

                        lib::bug_on(!vmspace->map(
                            address, fsize, prot, vmm::flag::private_,
                            inode->map(true), phdr.p_offset - misalign
                        ));

                        if (msize > fsize)
                        {
                            lib::bug_on(!vmspace->map(
                                address + fsize, msize - fsize,
                                prot, vmm::flag::private_,
                                std::make_shared<vmm::memobject>(), 0
                            ));
                        }
                        break;
                    }
                    case PT_PHDR:
                        aux.at_phdr = addr + phdr.p_vaddr;
                        break;
                    case PT_INTERP:
                    {
                        lib::membuffer buffer { phdr.p_filesz - 1 };
                        lib::bug_on(inode->read(
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

        bool identify(const std::shared_ptr<vfs::dentry> &file) override
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

        sched::thread *load(bin::exec::request &req,  sched::process *proc) override
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
            auto stack = reinterpret_cast<std::uintptr_t *>(addr);
            const auto val = [&stack] { return reinterpret_cast<std::uintptr_t>(stack); };

            for (const auto &env : req.envp)
            {
                stack = stack - env.length() - 1;
                std::memcpy(stack, env.c_str(), env.length());
            }

            for (const auto &arg : req.argv)
            {
                stack = stack - arg.length() - 1;
                std::memcpy(stack, arg.c_str(), arg.length());
            }

            stack = reinterpret_cast<std::uintptr_t *>(lib::align_down(val(), 16));
            if ((req.argv.size() + req.envp.size() + 1) & 1)
                stack--;

            *(--stack) = 0; *(--stack) = 0;
            stack -= 2; stack[0] = AT_ENTRY, stack[1] = auxv.at_entry;
            stack -= 2; stack[0] = AT_PHDR,  stack[1] = auxv.at_phdr;
            stack -= 2; stack[0] = AT_PHENT, stack[1] = auxv.at_phent;
            stack -= 2; stack[0] = AT_PHNUM, stack[1] = auxv.at_phnum;

            auto tmp = addr;

            *(--stack) = 0;
            stack -= req.envp.size();
            for (std::size_t i = 0; const auto &env : req.envp)
            {
                tmp -= env.length() + 1;
                stack[i++] = tmp;
            }

            *(--stack) = 0;
            stack -= req.argv.size();
            for (std::size_t i = 0; const auto &arg : req.argv)
            {
                tmp -= arg.length() + 1;
                stack[i++] = tmp;
            }

            *(--stack) = req.argv.size();

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