// Copyright (C) 2024-2025  ilobilo

module system.syscall.proc;

import system.scheduler;
import lib;
import cppstd;

namespace syscall::proc
{
    pid_t gettid()
    {
        return sched::this_thread()->tid;
    }

    pid_t getpid()
    {
        return sched::this_thread()->parent->pid;
    }

    pid_t getppid()
    {
        const auto parent = sched::this_thread()->parent->parent;
        return parent ? parent->pid : 0;
    }

    uid_t getuid() { return sched::this_thread()->parent->ruid;}
    uid_t geteuid() { return sched::this_thread()->parent->euid; }
    gid_t getgid() { return sched::this_thread()->parent->rgid; }
    gid_t getegid() { return sched::this_thread()->parent->egid; }

    int getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid)
    {
        const auto proc = sched::this_thread()->parent;

        if (ruid)
            lib::copy_to_user(ruid, &proc->ruid, sizeof(uid_t));
        if (euid)
            lib::copy_to_user(euid, &proc->euid, sizeof(uid_t));
        if (suid)
            lib::copy_to_user(suid, &proc->suid, sizeof(uid_t));

        return 0;
    }

    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid)
    {
        const auto proc = sched::this_thread()->parent;

        if (rgid)
            lib::copy_to_user(rgid, &proc->rgid, sizeof(gid_t));
        if (egid)
            lib::copy_to_user(egid, &proc->egid, sizeof(gid_t));
        if (sgid)
            lib::copy_to_user(sgid, &proc->sgid, sizeof(gid_t));

        return 0;
    }

    pid_t getpgid(pid_t pid)
    {
        const auto proc = sched::proc_for(pid);
        if (!proc)
            return (errno = ESRCH, -1);
        return proc->pgid;
    }

    int sigaction(int signum, const struct sigaction __user *act, struct sigaction __user *oldact)
    {
        lib::unused(signum, act, oldact);
        return (errno = ENOSYS, -1);
    }

    int sigprocmask(int how, const struct sigset_t __user *set, struct sigset_t __user *oldset, std::size_t sigsetsize)
    {
        lib::unused(how, set, oldset, sigsetsize);
        return (errno = ENOSYS, -1);
    }

    long futex(std::uint32_t __user *uaddr, int futex_op, std::uint32_t val, const timespec *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3)
    {
        lib::unused(uaddr, futex_op, val, timeout, uaddr2, val3);
        return 0;
        // return (errno = ENOSYS, -1);
    }

    struct rlimit
    {
        rlim_t rlim_cur;
        rlim_t rlim_max;
    };

    int prlimit(pid_t pid, int resource, const struct rlimit __user *new_limit, struct rlimit __user *old_limit)
    {
        lib::unused(pid, resource, new_limit, old_limit);
        return (errno = ENOSYS, -1);
    }

    [[noreturn]] void exit_group(int status)
    {
        lib::unused(status);
        lib::panic("todo: exit_group");
        std::unreachable();
    }
} // namespace syscall::proc