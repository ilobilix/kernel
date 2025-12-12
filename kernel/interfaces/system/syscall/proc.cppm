// Copyright (C) 2024-2025  ilobilo

export module system.syscall.proc;

import lib;
import std;

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
    int setpgid(pid_t pid, pid_t pgid);

    pid_t setsid();

    int getgroups(int size, gid_t __user *list);
    int setgroups(std::size_t size, const gid_t __user *list);

    int set_tid_address(int __user *tidptr);

    mode_t umask(mode_t mask);

    int sigaction(int signum, const struct sigaction __user *act, struct sigaction __user *oldact);
    int sigprocmask(int how, const struct sigset_t __user *set, struct sigset_t __user *oldset, std::size_t sigsetsize);
    int rseq(struct rseq __user *rseq, std::uint32_t rseq_len, int flags, std::uint32_t sig);

    struct fd_set;
    int select(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, timeval __user *timeout);
    int pselect(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, const timespec __user *timeout, const sigset_t __user *sigmask);

    long futex(std::uint32_t __user *uaddr, int futex_op, std::uint32_t val, const timespec __user *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3);
    long get_robust_list(int pid, struct robust_list_head __user *__user *head_ptr, std::size_t __user *sizep);
    long set_robust_list(struct robust_list_head __user *head, std::size_t size);

    int prlimit(pid_t pid, int resource, const struct rlimit __user *new_limit, struct rlimit __user *old_limit);

    long clone(unsigned long flags, void __user *stack, int __user *parent_tid, int __user *child_tid, unsigned long tls);
    long clone3(struct clone_args __user *cl_args, std::size_t size);

    pid_t fork();
    pid_t vfork();

    [[noreturn]] void exit_group(int status);
} // export namespace syscall::proc