// Copyright (C) 2024-2025  ilobilo

export module system.syscall.proc;

import lib;
import cppstd;

export namespace syscall::proc
{
    pid_t gettid();
    pid_t getpid();
    pid_t getppid();

    uid_t getuid();
    uid_t geteuid();

    gid_t getgid();
    gid_t getegid();

    int getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);

    pid_t getpgid(pid_t pid);

    int sigaction(int signum, const struct sigaction __user *act, struct sigaction __user *oldact);
    int sigprocmask(int how, const struct sigset_t __user *set, struct sigset_t __user *oldset, std::size_t sigsetsize);

    long futex(std::uint32_t __user *uaddr, int futex_op, std::uint32_t val, const timespec *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3);

    int prlimit(pid_t pid, int resource, const struct rlimit __user *new_limit, struct rlimit __user *old_limit);

    [[noreturn]] void exit_group(int status);
} // export namespace syscall::proc