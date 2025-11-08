// Copyright (C) 2024-2025  ilobilo

module x86_64.system.syscall;

import :arch;

import x86_64.system.gdt;
import system.syscall;
import system.cpu;
import arch;
import lib;
import cppstd;

namespace x86_64::syscall
{
    struct getter
    {
        static std::array<std::uintptr_t, 6> get_args(cpu::registers *regs)
        {
            return { regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8, regs->r9 };
        }
    };

    using namespace ::syscall;
    lib::syscall::entry<6, getter> table[]
    {
        [9] = { "mmap", memory::mmap },
        [11] = { "munmap", memory::munmap },
        [158] = { "arch_prctl", arch::arch_prctl },
        [186] = { "gettid", proc::gettid }
    };

    extern "C" void syscall_entry();
    extern "C" void syscall_handler(cpu::registers *regs)
    {
        const auto idx = regs->rax;
        if (idx >= std::size(table) || !table[idx].is_valid())
            lib::panic("invalid syscall: {}", idx);

        regs->rax = table[idx].invoke(regs);
    }

    void init_cpu()
    {
        // IA32_EFER syscall
        cpu::msr::write(0xC0000080, cpu::msr::read(0xC0000080) | (1 << 0));
        // IA32_STAR set segments
        cpu::msr::write(0xC0000081,
            ((static_cast<std::uint64_t>(gdt::segment::ucode32) | 0x03) << 48) |
            (static_cast<std::uint64_t>(gdt::segment::code) << 32)
        );
        // IA32_LSTAR handler
        cpu::msr::write(0xC0000082, reinterpret_cast<std::uintptr_t>(syscall_entry));
        // IA32_FMASK rflags mask
        cpu::msr::write(0xC0000084, ~2u);

        // TODO: int 0x80?
    }
} // namespace x86_64::syscall