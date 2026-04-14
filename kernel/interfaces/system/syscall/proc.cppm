// Copyright (C) 2024-2026  ilobilo

export module system.syscall.proc;

import lib;
import std;

export namespace syscall::proc
{
    pid_t gettid();
    pid_t getpid();
    pid_t getppid();
    pid_t getpgrp();

    uid_t getuid();
    uid_t geteuid();

    gid_t getgid();
    gid_t getegid();

    int getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);

    pid_t getpgid(pid_t pid);
    int setpgid(pid_t pid, pid_t pgid);

    pid_t setsid();

    int setfsuid(uid_t fsuid);
    int setfsgid(gid_t fsgid);

    int getgroups(int size, gid_t __user *list);
    int setgroups(std::size_t size, const gid_t __user *list);

    int set_tid_address(int __user *tidptr);

    mode_t umask(mode_t mask);

    int sigaction(int signum, const struct sigaction __user *act, struct sigaction __user *oldact);
    int sigprocmask(
        int how, const struct sigset_t __user *set,
        struct sigset_t __user *oldset, std::size_t sigsetsize
    );
    int sigaltstack(const struct stack_t __user *ss, stack_t __user *old_ss);

    int rseq(struct rseq __user *rseq, std::uint32_t rseq_len, int flags, std::uint32_t sig);

    long futex(
        std::uint32_t __user *uaddr, int futex_op, std::uint32_t val,
        const timespec __user *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3
    );

    long get_robust_list(
        int pid, struct robust_list_head __user *__user *head_ptr,
        std::size_t __user *sizep
    );
    long set_robust_list(struct robust_list_head __user *head, std::size_t size);

    int prlimit(
        pid_t pid, int resource, const struct rlimit __user *new_limit,
        struct rlimit __user *old_limit
    );

    long clone(
        unsigned long flags, void __user *stack, int __user *parent_tid,
        int __user *child_tid, unsigned long tls
    );
    long clone3(struct clone_args __user *cl_args, std::size_t size);

    pid_t fork();
    pid_t vfork();

    int execveat(
        int dirfd, const char __user *pathname, const char __user *const __user *argv,
        const char __user *const __user *envp, int flags
    );

    int execve(
        const char __user *pathname, const char __user *const __user *argv,
        const char __user *const __user *envp
    );

    pid_t wait4(pid_t pid, int __user *wstatus, int options, struct rusage __user *rusage);

    [[noreturn]] void exit_group(int status);

    int tgkill(pid_t tgid, pid_t tid, int sig);
} // export namespace syscall::proc
