// Copyright (C) 2024-2026  ilobilo

export module system.syscall.proc;

import system.sched;
import lib;
import std;

export namespace syscall::proc
{
    pid_t gettid();
    pid_t getpid();
    pid_t getppid();
    pid_t getpgrp();

    uid_t getuid();
    int setuid(uid_t uid);

    gid_t getgid();
    int setgid(gid_t gid);

    uid_t geteuid();
    gid_t getegid();

    int setreuid(uid_t ruid, uid_t euid);
    int setregid(gid_t rgid, gid_t egid);

    int getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
    int setresuid(uid_t ruid, uid_t euid, uid_t suid);

    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);
    int setresgid(gid_t rgid, gid_t egid, gid_t sgid);

    pid_t getpgid(pid_t pid);
    int setpgid(pid_t pid, pid_t pgid);

    pid_t getsid(pid_t pid);
    pid_t setsid();

    int getpriority(int which, int who);
    int setpriority(int which, int who, int prio);

    int setfsuid(uid_t fsuid);
    int setfsgid(gid_t fsgid);

    int getgroups(int size, gid_t __user *list);
    int setgroups(std::size_t size, const gid_t __user *list);

    int set_tid_address(int __user *tidptr);

    unsigned int alarm(unsigned int seconds);

    int setitimer(int which, const itimerval __user *new_value, itimerval __user *old_value);
    int getitimer(int which, itimerval __user *curr_value);

    int kill(pid_t pid, int sig);
    int tgkill(pid_t tgid, pid_t tid, int sig);

    int rt_sigaction(
        int signum, const sched::sigaction_t __user *act,
        sched::sigaction_t __user *oldact, std::size_t sigsetsize
    );
    int rt_sigprocmask(
        int how, const sched::sigset_t __user *set,
        sched::sigset_t __user *oldset, std::size_t sigsetsize
    );
    int rt_sigpending(sched::sigset_t __user *set, std::size_t sigsetsize);
    int rt_sigtimedwait(
        const sched::sigset_t __user *uthese, sched::siginfo_t __user *uinfo,
        const timespec __user *uts, std::size_t sigsetsize
    );
    int sigaltstack(const sched::stack_t __user *ss, sched::stack_t __user *old_ss);

    std::uintptr_t rt_sigreturn();

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
        pid_t pid, int resource, const sched::rlimit __user *new_limit,
        sched::rlimit __user *old_limit
    );
    int getrlimit(int resource, sched::rlimit __user *rlim);
    int setrlimit(int resource, const sched::rlimit __user *rlim);

    int getrusage(int who, struct rusage __user *usage);

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
} // export namespace syscall::proc
