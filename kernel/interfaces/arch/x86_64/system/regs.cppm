// Copyright (C) 2024-2026  ilobilo

export module system.cpu.regs:impl;
import std;

export namespace cpu
{
    struct registers
    {
        std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        std::uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
        std::uint64_t vector, error_code, rip, cs, rflags, rsp, ss;

        std::uintptr_t fp() { return rbp; }
        std::uintptr_t ip() { return rip; }
    };
} // export namespace cpu