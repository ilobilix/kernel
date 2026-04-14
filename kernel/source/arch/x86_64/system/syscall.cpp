// Copyright (C) 2024-2025  ilobilo

module x86_64.system.syscall;

import x86_64.system.gdt;
import system.cpu;
import lib;
import std;

namespace x86_64::syscall
{
    extern "C" void syscall_entry();
    extern "C" void syscall_handler(cpu::registers *regs)
    {
        // args: rdi rsi rdx r10 r8 r9
        log::debug("syscall {}", regs->rax);
        // TODO
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