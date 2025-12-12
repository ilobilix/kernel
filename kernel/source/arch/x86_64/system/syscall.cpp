// Copyright (C) 2024-2025  ilobilo

module x86_64.system.syscall;

import :arch;

import x86_64.system.gdt;
import system.syscall;
import system.cpu.local;
import system.cpu;
import arch;
import lib;
import std;

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
        [0] = { "read", vfs::read },
        [1] = { "write", vfs::write },
        [2] = { "open", vfs::open },
        [3] = { "close", vfs::close },
        [4] = { "stat", vfs::stat },
        [5] = { "fstat", vfs::fstat },
        [6] = { "lstat", vfs::lstat },
        [8] = { "lseek", vfs::lseek },
        [9] = { "mmap", memory::mmap },
        [10] = { "mprotect", memory::mprotect },
        [11] = { "munmap", memory::munmap },
        [12] = { "brk", memory::brk },
        [13] = { "sigaction", proc::sigaction },
        [14] = { "sigprocmask", proc::sigprocmask },
        [16] = { "ioctl", vfs::ioctl },
        [17] = { "pread", vfs::pread },
        [18] = { "pwrite", vfs::pwrite },
        [19] = { "readv", vfs::readv },
        [20] = { "writev", vfs::writev },
        [21] = { "access", vfs::access },
        [22] = { "pipe", vfs::pipe },
        [23] = { "select", proc::select },
        [32] = { "dup", vfs::dup },
        [33] = { "dup2", vfs::dup2 },
        [39] = { "getpid", proc::getpid },
        [41] = { "socket", vfs::socket },
        [56] = { "clone", proc::clone },
        [57] = { "fork", proc::fork },
        [58] = { "vfork", proc::vfork },
        [63] = { "uname", misc::uname },
        [72] = { "fcntl", vfs::fcntl },
        [79] = { "getcwd", vfs::getcwd, [](std::uintptr_t val) { return val == 0; } },
        [85] = { "creat", vfs::creat },
        [95] = { "umask", proc::umask },
        [96] = { "gettimeofday", chrono::gettimeofday },
        [102] = { "getuid", proc::getuid },
        [104] = { "getgid", proc::getgid },
        [107] = { "geteuid", proc::geteuid },
        [108] = { "getegid", proc::getegid },
        [109] = { "setpgid", proc::setpgid },
        [110] = { "getppid", proc::getppid },
        [112] = { "setsid", proc::setsid },
        [115] = { "getgroups", proc::getgroups },
        [116] = { "setgroups", proc::setgroups },
        [118] = { "getresuid", proc::getresuid },
        [120] = { "getresgid", proc::getresgid },
        [121] = { "getpgid", proc::getpgid },
        [158] = { "arch_prctl", arch::arch_prctl },
        [164] = { "settimeofday", chrono::settimeofday },
        [169] = { "reboot", misc::reboot },
        [186] = { "gettid", proc::gettid },
        [202] = { "futex", proc::futex },
        [218] = { "set_tid_address", proc::set_tid_address },
        [228] = { "clock_gettime", chrono::clock_gettime },
        [231] = { "exit_group", proc::exit_group },
        [257] = { "openat", vfs::openat },
        [262] = { "fstatat", vfs::fstatat },
        [269] = { "faccessat", vfs::faccessat },
        [270] = { "pselect6", proc::pselect },
        [273] = { "set_robust_list", proc::set_robust_list },
        [274] = { "get_robust_list", proc::get_robust_list },
        [292] = { "dup3", vfs::dup3 },
        [293] = { "pipe2", vfs::pipe2 },
        [295] = { "preadv", vfs::preadv },
        [296] = { "pwritev", vfs::pwritev },
        [302] = { "prlimit", proc::prlimit },
        [318] = { "getrandom", misc::getrandom },
        [334] = { "rseq", proc::rseq },
        [435] = { "clone3", proc::clone3 }
    };

    cpu_local<bool> in_syscall;
    cpu_local_init(in_syscall, false);

    bool is_in_syscall()
    {
        return in_syscall.get();
    }

    extern "C" void syscall_entry();
    extern "C" void syscall_handler(cpu::registers *regs)
    {
        const auto idx = regs->rax;
        if (idx >= std::size(table) || !table[idx].is_valid())
            lib::panic("invalid syscall: {}", idx);

        in_syscall = true;
        regs->rax = table[idx].invoke(regs);
        in_syscall = false;
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