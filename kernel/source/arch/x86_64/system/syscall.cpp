// Copyright (C) 2024-2026  ilobilo

module x86_64.system.syscall;

import :arch;

import x86_64.system.gdt;
import system.syscall;
import system.cpu.local;
import system.cpu;
import system.sched;
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
        [0] = { "read", vfs::read, true },
        [1] = { "write", vfs::write, true },
        [2] = { "open", vfs::open, true },
        [3] = { "close", vfs::close },
        [4] = { "stat", vfs::stat },
        [5] = { "fstat", vfs::fstat },
        [6] = { "lstat", vfs::lstat },
        [7] = { "poll", vfs::poll, true },
        [8] = { "lseek", vfs::lseek, true },
        [9] = { "mmap", memory::mmap, true },
        [10] = { "mprotect", memory::mprotect },
        [11] = { "munmap", memory::munmap },
        [12] = { "brk", memory::brk, true },
        [13] = { "rt_sigaction", proc::rt_sigaction },
        [14] = { "rt_sigprocmask", proc::rt_sigprocmask },
        [15] = { "rt_sigreturn", proc::rt_sigreturn },
        [16] = { "ioctl", vfs::ioctl },
        [17] = { "pread", vfs::pread, true },
        [18] = { "pwrite", vfs::pwrite, true },
        [19] = { "readv", vfs::readv, true },
        [20] = { "writev", vfs::writev, true },
        [21] = { "access", vfs::access },
        [22] = { "pipe", vfs::pipe },
        [23] = { "select", vfs::select, true },
        [24] = { "sched_yield", proc::sched_yield },
        [25] = { "mremap", memory::mremap, true },
        [27] = { "mincore", memory::mincore },
        [28] = { "madvise", memory::madvise },
        [32] = { "dup", vfs::dup, true },
        [33] = { "dup2", vfs::dup2, true },
        [34] = { "pause", proc::pause },
        [35] = { "nanosleep", chrono::nanosleep },
        [36] = { "getitimer", proc::getitimer },
        [37] = { "alarm", proc::alarm, true },
        [38] = { "setitimer", proc::setitimer },
        [39] = { "getpid", proc::getpid, true },
        [41] = { "socket", vfs::socket, true },
        [42] = { "connect", vfs::connect },
        [43] = { "accept", vfs::accept, true },
        [44] = { "sendto", vfs::sendto, true },
        [45] = { "recvfrom", vfs::recvfrom, true },
        [46] = { "sendmsg", vfs::sendmsg, true },
        [47] = { "recvmsg", vfs::recvmsg, true },
        [48] = { "shutdown", vfs::shutdown },
        [49] = { "bind", vfs::bind },
        [50] = { "listen", vfs::listen },
        [51] = { "getsockname", vfs::getsockname },
        [52] = { "getpeername", vfs::getpeername },
        [53] = { "socketpair", vfs::socketpair },
        [54] = { "setsockopt", vfs::setsockopt },
        [55] = { "getsockopt", vfs::getsockopt },
        [56] = { "clone", proc::clone, true },
        [57] = { "fork", proc::fork, true },
        [58] = { "vfork", proc::vfork, true },
        [59] = { "execve", proc::execve },
        [60] = { "exit", proc::exit },
        [61] = { "wait4", proc::wait4, true },
        [62] = { "kill", proc::kill },
        [63] = { "uname", misc::uname },
        [72] = { "fcntl", vfs::fcntl, true },
        [73] = { "flock", vfs::flock },
        [74] = { "fsync", vfs::fsync },
        [76] = { "truncate", vfs::truncate },
        [77] = { "ftruncate", vfs::ftruncate },
        [79] = { "getcwd", vfs::getcwd, true },
        [80] = { "chdir", vfs::chdir },
        [81] = { "fchdir", vfs::fchdir },
        [82] = { "rename", vfs::rename },
        [83] = { "mkdir", vfs::mkdir },
        [84] = { "rmdir", vfs::rmdir },
        [85] = { "creat", vfs::creat, true },
        [86] = { "link", vfs::link },
        [87] = { "unlink", vfs::unlink },
        [88] = { "symlink", vfs::symlink },
        [89] = { "readlink", vfs::readlink, true },
        [90] = { "chmod", vfs::chmod },
        [91] = { "fchmod", vfs::fchmod },
        [92] = { "chown", vfs::chown },
        [93] = { "fchown", vfs::fchown },
        [94] = { "lchown", vfs::lchown },
        [95] = { "umask", vfs::umask, true },
        [96] = { "gettimeofday", chrono::gettimeofday },
        [97] = { "getrlimit", proc::getrlimit },
        [98] = { "getrusage", proc::getrusage },
        [99] = { "sysinfo", misc::sysinfo },
        [102] = { "getuid", proc::getuid, true },
        [104] = { "getgid", proc::getgid, true },
        [105] = { "setuid", proc::setuid },
        [106] = { "setgid", proc::setgid },
        [107] = { "geteuid", proc::geteuid, true },
        [108] = { "getegid", proc::getegid, true },
        [103] = { "syslog", misc::syslog, true },
        [109] = { "setpgid", proc::setpgid },
        [110] = { "getppid", proc::getppid, true },
        [111] = { "getpgrp", proc::getpgrp, true },
        [112] = { "setsid", proc::setsid, true },
        [113] = { "setreuid", proc::setreuid },
        [114] = { "setregid", proc::setregid },
        [115] = { "getgroups", proc::getgroups, true },
        [116] = { "setgroups", proc::setgroups },
        [117] = { "setresuid", proc::setresuid },
        [118] = { "getresuid", proc::getresuid },
        [119] = { "setresgid", proc::setresgid },
        [120] = { "getresgid", proc::getresgid },
        [121] = { "getpgid", proc::getpgid, true },
        [122] = { "setfsuid", proc::setfsuid, true },
        [123] = { "setfsgid", proc::setfsgid, true },
        [124] = { "getsid", proc::getsid, true },
        [125] = { "capget", proc::capget },
        [126] = { "capset", proc::capset },
        [127] = { "rt_sigpending", proc::rt_sigpending },
        [128] = { "rt_sigtimedwait", proc::rt_sigtimedwait, true },
        [130] = { "rt_sigsuspend", proc::rt_sigsuspend },
        [131] = { "sigaltstack", proc::sigaltstack },
        [133] = { "mknod", vfs::mknod },
        [137] = { "statfs", vfs::statfs },
        [138] = { "fstatfs", vfs::fstatfs },
        [140] = { "getpriority", proc::getpriority, true },
        [141] = { "setpriority", proc::setpriority },
        [153] = { "vhangup", misc::vhangup },
        [157] = { "prctl", misc::prctl, true },
        [158] = { "arch_prctl", arch::arch_prctl },
        [160] = { "setrlimit", proc::setrlimit },
        [164] = { "settimeofday", chrono::settimeofday },
        [165] = { "mount", vfs::mount },
        [169] = { "reboot", misc::reboot },
        [170] = { "sethostname", misc::sethostname },
        [175] = { "init_module", vfs::init_module },
        [186] = { "gettid", proc::gettid, true },
        [188] = { "setxattr", vfs::setxattr },
        [189] = { "lsetxattr", vfs::lsetxattr },
        [190] = { "fsetxattr", vfs::fsetxattr },
        [191] = { "getxattr", vfs::getxattr, true },
        [192] = { "lgetxattr", vfs::lgetxattr, true },
        [193] = { "fgetxattr", vfs::fgetxattr, true },
        [194] = { "listxattr", vfs::listxattr, true },
        [195] = { "llistxattr", vfs::llistxattr, true },
        [196] = { "flistxattr", vfs::flistxattr, true },
        [197] = { "removexattr", vfs::removexattr },
        [198] = { "lremovexattr", vfs::lremovexattr },
        [199] = { "fremovexattr", vfs::fremovexattr },
        [201] = { "time", chrono::time, true },
        [202] = { "futex", proc::futex, true },
        [203] = { "sched_setaffinity", proc::sched_setaffinity },
        [204] = { "sched_getaffinity", proc::sched_getaffinity, true },
        [213] = { "epoll_create", vfs::epoll_create, true },
        [217] = { "getdents64", vfs::getdents64, true },
        [218] = { "set_tid_address", proc::set_tid_address, true },
        [221] = { "fadvise64", vfs::fadvise64 },
        [228] = { "clock_gettime", chrono::clock_gettime },
        [229] = { "clock_getres", chrono::clock_getres },
        [230] = { "clock_nanosleep", chrono::clock_nanosleep },
        [231] = { "exit_group", proc::exit_group },
        [232] = { "epoll_wait", vfs::epoll_wait, true },
        [233] = { "epoll_ctl", vfs::epoll_ctl },
        [234] = { "tgkill", proc::tgkill },
        [247] = { "waitid", proc::waitid, true },
        [253] = { "inotify_init", vfs::inotify_init, true },
        [254] = { "inotify_add_watch", vfs::inotify_add_watch, true },
        [255] = { "inotify_rm_watch", vfs::inotify_rm_watch },
        [257] = { "openat", vfs::openat, true },
        [258] = { "mkdirat", vfs::mkdirat },
        [259] = { "mknodat", vfs::mknodat },
        [260] = { "fchownat", vfs::fchownat },
        [262] = { "fstatat", vfs::fstatat },
        [263] = { "unlinkat", vfs::unlinkat },
        [264] = { "renameat", vfs::renameat },
        [265] = { "linkat", vfs::linkat },
        [266] = { "symlinkat", vfs::symlinkat },
        [267] = { "readlinkat", vfs::readlinkat, true },
        [268] = { "fchmodat", vfs::fchmodat },
        [269] = { "faccessat", vfs::faccessat },
        [270] = { "pselect6", vfs::pselect6, true },
        [271] = { "ppoll", vfs::ppoll, true },
        [273] = { "set_robust_list", proc::set_robust_list },
        [274] = { "get_robust_list", proc::get_robust_list },
        [275] = { "splice", vfs::splice, true },
        [280] = { "utimensat", vfs::utimensat },
        [281] = { "epoll_pwait", vfs::epoll_pwait, true },
        [282] = { "signalfd", vfs::signalfd, true },
        [284] = { "eventfd", vfs::eventfd, true },
        [288] = { "accept4", vfs::accept4, true },
        [289] = { "signalfd4", vfs::signalfd4, true },
        [290] = { "eventfd2", vfs::eventfd2, true },
        [291] = { "epoll_create1", vfs::epoll_create1, true },
        [292] = { "dup3", vfs::dup3, true },
        [293] = { "pipe2", vfs::pipe2 },
        [294] = { "inotify_init1", vfs::inotify_init1, true },
        [295] = { "preadv", vfs::preadv, true },
        [296] = { "pwritev", vfs::pwritev, true },
        [302] = { "prlimit", proc::prlimit },
        [313] = { "finit_module", vfs::finit_module },
        [316] = { "renameat2", vfs::renameat2 },
        [317] = { "seccomp", misc::seccomp, true },
        [318] = { "getrandom", misc::getrandom, true },
        [322] = { "execveat", proc::execveat },
        [332] = { "statx", vfs::statx },
        [334] = { "rseq", proc::rseq },
        [430] = { "fsopen", vfs::fsopen, true },
        [435] = { "clone3", proc::clone3, true },
        [436] = { "close_range", vfs::close_range },
        [439] = { "faccessat2", vfs::faccessat2 },
        [441] = { "epoll_pwait2", vfs::epoll_pwait2, true },
        [444] = { "landlock_create_ruleset", misc::landlock_create_ruleset, true },
        [452] = { "fchmodat2", vfs::fchmodat2 }
    };

    extern "C" void syscall_entry();
    extern "C" void syscall_handler(cpu::registers *regs)
    {
        const auto idx = regs->rax;
        if (idx >= std::size(table) || !table[idx].is_valid())
        {
            lib::error("invalid syscall: {}", idx);
            regs->rax = -ENOSYS;
            return;
        }

        sched::current_thread()->saved_regs = regs;
        regs->rax = table[idx].invoke(regs);

        sched::die_if_kill_pending();
        sched::handle_pending_signals(regs);
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
