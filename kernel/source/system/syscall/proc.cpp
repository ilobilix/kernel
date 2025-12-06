// Copyright (C) 2024-2025  ilobilo

module system.syscall.proc;

import system.scheduler;
import lib;
import std;

namespace syscall::proc
{
    namespace
    {
        template<typename Type>
        inline std::optional<Type> copy_from(const Type __user *uptr)
        {
            if (uptr == nullptr)
                return std::nullopt;
            Type val;
            if (!lib::copy_from_user(&val, uptr, sizeof(Type)))
                return std::nullopt;
            return val;
        }

        template<typename Type>
        inline void copy_to(Type __user *uptr, const Type &val)
        {
            if (uptr == nullptr)
                return;
            lib::copy_to_user(uptr, &val, sizeof(Type));
        }
    } // namespace

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
        {
            if (!lib::copy_to_user(ruid, &proc->ruid, sizeof(uid_t)))
                return (errno = EFAULT, -1);
        }
        if (euid)
        {
            if (!lib::copy_to_user(euid, &proc->euid, sizeof(uid_t)))
                return (errno = EFAULT, -1);
        }
        if (suid)
        {
            if (!lib::copy_to_user(suid, &proc->suid, sizeof(uid_t)))
                return (errno = EFAULT, -1);
        }

        return 0;
    }

    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid)
    {
        const auto proc = sched::this_thread()->parent;

        if (rgid)
        {
            if (!lib::copy_to_user(rgid, &proc->rgid, sizeof(gid_t)))
                return (errno = EFAULT, -1);
        }
        if (egid)
        {
            if (!lib::copy_to_user(egid, &proc->egid, sizeof(gid_t)))
                return (errno = EFAULT, -1);
        }
        if (sgid)
        {
            if (!lib::copy_to_user(sgid, &proc->sgid, sizeof(gid_t)))
                return (errno = EFAULT, -1);
        }

        return 0;
    }

    pid_t getpgid(pid_t pid)
    {
        const auto proc = sched::proc_for(pid);
        if (!proc)
            return (errno = ESRCH, -1);
        return proc->pgid;
    }

    int setpgid(pid_t pid, pid_t pgid)
    {
        if (pgid < 0)
            return (errno = EINVAL, -1);

        const auto proc = sched::this_thread()->parent;
        if (pid == 0)
            pid = proc->pid;
        if (pgid == 0)
            pgid = pid;

        const auto target = sched::proc_for(pid);
        if (!target)
            return (errno = ESRCH, -1);

        // is leader
        if (pid == target->sid)
            return (errno = EPERM, -1);

        if (proc->children.contains(pid))
        {
            if (target->has_execved)
                return (errno = EACCES, -1);
            if (proc->sid != target->sid)
                return (errno = EPERM, -1);
        }
        else if (pid != proc->pid)
            return (errno = ESRCH, -1);

        auto target_group = sched::group_for(pgid);
        if (!target_group || target_group->sid != target->sid)
            return (errno = EPERM, -1);

        if (!sched::change_group(target, target_group))
            return (errno = EINVAL, -1);
        return (errno = no_error, 0);
    }

    int getgroups(int size, gid_t __user *list)
    {
        if (size < 0)
            return (errno = EINVAL, -1);

        const auto proc = sched::this_thread()->parent;
        const auto supgids = proc->supplementary_gids.read_lock();
        const auto num = supgids->size();

        if (size == 0)
            return num;
        if (static_cast<std::size_t>(size) < num)
            return (errno = EINVAL, -1);

        if (!lib::copy_to_user(list, supgids->data(), num * sizeof(gid_t)))
            return (errno = EFAULT, -1);

        return num;
    }

    #define NGROUPS_MAX 65536
    int setgroups(std::size_t size, const gid_t __user *list)
    {
        // TODO: capability check
        if (size > NGROUPS_MAX)
            return (errno = EINVAL, -1);

        std::vector<gid_t> supgids(size);
        if (!lib::copy_from_user(supgids.data(), list, size * sizeof(gid_t)))
            return (errno = EFAULT, -1);

        const auto proc = sched::this_thread()->parent;
        proc->supplementary_gids.write_lock().value() = std::move(supgids);

        return 0;
    }

    int set_tid_address(int __user *tidptr)
    {
        auto thread = sched::this_thread();
        thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(tidptr);
        return thread->tid;
    }

    int sigaction(int signum, const struct sigaction __user *act, struct sigaction __user *oldact)
    {
        // TODO
        lib::unused(signum, act, oldact);
        return (errno = ENOSYS, -1);
    }

    int sigprocmask(int how, const struct sigset_t __user *set, struct sigset_t __user *oldset, std::size_t sigsetsize)
    {
        // TODO
        lib::unused(how, set, oldset, sigsetsize);
        return (errno = ENOSYS, -1);
    }

    int rseq(struct rseq __user *rseq, std::uint32_t rseq_len, int flags, std::uint32_t sig)
    {
        // TODO
        lib::unused(rseq, rseq_len, flags, sig);
        return (errno = ENOSYS, -1);
    }

    constexpr int FD_SETSIZE = 1024;
    struct [[aligned(alignof(long))]] fd_set
    {
        std::uint8_t fds_bits[FD_SETSIZE / 8];
    };
    static_assert(sizeof(fd_set) == FD_SETSIZE / 8);

    struct sigset_t
    {
        unsigned long sig[1024 / (8 * sizeof(long))];
    };

    namespace
    {
        inline void FD_CLR(int fd, fd_set *set)
        {
            lib::bug_on(fd >= FD_SETSIZE);
            set->fds_bits[fd / 8] &= ~(1 << (fd % 8));
        }

        inline int FD_ISSET(int fd, const fd_set *set)
        {
            lib::bug_on(fd >= FD_SETSIZE);
            return set->fds_bits[fd / 8] & (1 << (fd % 8));
        }

        inline void FD_SET(int fd, fd_set *set) {
            lib::bug_on(fd >= FD_SETSIZE);
            set->fds_bits[fd / 8] |= 1 << (fd % 8);
        }

        inline void FD_ZERO(fd_set *set)
        {
            std::memset(set->fds_bits, 0, sizeof(fd_set));
        }

        int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, timespec *timeout, bool update_timeout, const sigset_t *sigmask)
        {
            auto count_bits = [](const fd_set *set) -> std::size_t
            {
                if (set == nullptr)
                    return 0;

                std::size_t count = 0;
                for (std::size_t i = 0; i < sizeof(set->fds_bits); i++)
                    count += std::popcount(set->fds_bits[i]);
                return count;
            };

            // TODO
            lib::unused(nfds, timeout, update_timeout, sigmask);
            lib::unused(FD_CLR, FD_ISSET, FD_SET, FD_ZERO);

            const int total_fds =
                count_bits(readfds) +
                count_bits(writefds) +
                count_bits(exceptfds);

            return total_fds;
        }

        int pselect(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, timespec *timeout, bool update_timeout, const sigset_t __user *sigmask)
        {
            auto kreadfds = copy_from(readfds);
            auto kwritefds = copy_from(writefds);
            auto kexceptfds = copy_from(exceptfds);
            auto ksigmask = copy_from(sigmask);

            const auto ret = pselect(
                nfds,
                kreadfds ? &kreadfds.value() : nullptr,
                kwritefds ? &kwritefds.value() : nullptr,
                kexceptfds ? &kexceptfds.value() : nullptr,
                timeout, update_timeout,
                ksigmask ? &ksigmask.value() : nullptr
            );

            if (kreadfds.has_value())
                copy_to(readfds, kreadfds.value());
            if (kwritefds.has_value())
                copy_to(writefds, kwritefds.value());
            if (kexceptfds.has_value())
                copy_to(exceptfds, kexceptfds.value());

            return ret;
        }
    } // namespace

    int select(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, timeval __user *timeout)
    {
        auto ktimeval = copy_from(timeout);
        std::optional<timespec> ktimeout { };
        if (ktimeval.has_value())
            ktimeout.emplace(ktimeval.value());

        return pselect(
            nfds, readfds, writefds, exceptfds,
            ktimeout ? &ktimeout.value() : nullptr,
            (timeout != nullptr), nullptr
        );
    }

    int pselect(int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds, const timespec __user *timeout, const sigset_t __user *sigmask)
    {
        auto ktimeout = copy_from(timeout);
        return pselect(
            nfds, readfds, writefds, exceptfds,
            ktimeout ? &ktimeout.value() : nullptr,
            false, sigmask
        );
    }

    long futex(std::uint32_t __user *uaddr, int futex_op, std::uint32_t val, const timespec __user *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3)
    {
        // TODO
        lib::unused(uaddr, futex_op, val, timeout, uaddr2, val3);
        // return (errno = ENOSYS, -1);
        return 0;
    }

    long get_robust_list(int pid, struct robust_list_head __user *__user *head_ptr, std::size_t __user *sizep)
    {
        // TODO
        lib::unused(pid, head_ptr, sizep);
        return (errno = ENOSYS, -1);
    }

    long set_robust_list(struct robust_list_head __user *head, std::size_t size)
    {
        // TODO
        lib::unused(head, size);
        return (errno = ENOSYS, -1);
    }

    struct rlimit
    {
        rlim_t rlim_cur;
        rlim_t rlim_max;
    };

    int prlimit(pid_t pid, int resource, const struct rlimit __user *new_limit, struct rlimit __user *old_limit)
    {
        // TODO
        lib::unused(pid, resource, new_limit, old_limit);
        return (errno = ENOSYS, -1);
    }

    namespace
    {
        struct kclone_args
        {
            std::uint64_t flags;
            int __user *pidfd;
            int __user *child_tid;
            int __user *parent_tid;
            int exit_signal;
            std::uint64_t stack;
            std::uint64_t stack_size;
            std::uint64_t tls;
            pid_t *set_tid;
            std::size_t set_tid_size;
            int cgroup;
        };

        enum clone_flags : std::uint64_t
        {
            csignal = 0x000000FF, // signal mask to be sent at exit
            clone_vm = 0x00000100, // set if VM shared between processes
            clone_fs = 0x00000200, // set if fs info shared between processes
            clone_files = 0x00000400, // set if open files shared between processes
            clone_sighand = 0x00000800, // set if signal handlers and blocked signals shared
            clone_pidfd = 0x00001000, // set if a pidfd should be placed in parent
            clone_ptrace = 0x00002000, // set if we want to let tracing continue on the child too
            clone_vfork = 0x00004000, // set if the parent wants the child to wake it up on mm_release
            clone_parent = 0x00008000, // set if we want to have the same parent as the cloner
            clone_thread = 0x00010000, // same thread group?
            clone_newns = 0x00020000, // new mount namespace group
            clone_sysvsem = 0x00040000, // share system V SEM_UNDO semantics
            clone_settls = 0x00080000, // create a new TLS for the child
            clone_parent_settid = 0x00100000, // set the TID in the parent
            clone_child_cleartid = 0x00200000, // clear the TID in the child
            clone_detached = 0x00400000, // unused, ignored
            clone_untraced = 0x00800000, // set if the tracing process can't force CLONE_PTRACE on this clone
            clone_child_settid = 0x01000000, // set the TID in the child
            clone_newcgroup = 0x02000000, // new cgroup namespace
            clone_newuts = 0x04000000, // new utsname namespace
            clone_newipc = 0x08000000, // new ipc namespace
            clone_newuser = 0x10000000, // new user namespace
            clone_newpid = 0x20000000, // new pid namespace
            clone_newnet = 0x40000000, // new network namespace
            clone_io = 0x80000000, // clone io context
            clone_clear_sighand = 0x100000000ull, // clear any signal handler and reset to SIG_DFL.
            clone_into_cgroup = 0x200000000ull, // clone into a specific cgroup given the right permissions.
            clone_newtime = 0x00000080 // new time namespace

        };

        pid_t kclone(const kclone_args &args)
        {
            // TODO
            lib::unused(args);
            return (errno = ENOSYS, -1);
        }
    } // namespace

    long clone(unsigned long flags, void __user *stack, int __user *parent_tid, int __user *child_tid, unsigned long tls)
    {
        return kclone({
            .flags = (flags & 0xFFFFFFFF) & ~csignal,
            .pidfd = parent_tid,
            .child_tid = child_tid,
            .parent_tid = parent_tid,
            .exit_signal = static_cast<int>((flags & 0xFFFFFFFF) & csignal),
            .stack = reinterpret_cast<std::uintptr_t>(stack),
            .stack_size = 0,
            .tls = tls,
            .set_tid = nullptr,
            .set_tid_size = 0,
            .cgroup = -1,
        });
    }

    struct clone_args
    {
        std::uint64_t flags;
        std::uint64_t pidfd;
        std::uint64_t child_tid;
        std::uint64_t parent_tid;
        std::uint64_t exit_signal;
        std::uint64_t stack;
        std::uint64_t stack_size;
        std::uint64_t tls;
        std::uint64_t set_tid;
        std::uint64_t set_tid_size;
        std::uint64_t cgroup;
    };

    long clone3(clone_args __user *cl_args, std::size_t size)
    {
        pid_t set_tid[32] { };

        clone_args uargs { };
        if (size < 64 || size > sizeof(clone_args))
            return (errno = EINVAL, -1);

        if (!lib::copy_from_user(&uargs, cl_args, size))
            return (errno = EFAULT, -1);

        if (uargs.set_tid_size > 32)
            return (errno = EINVAL, -1);

        if (!uargs.set_tid && uargs.set_tid_size > 0)
            return (errno = EINVAL, -1);

        if (uargs.set_tid && uargs.set_tid_size == 0)
            return (errno = EINVAL, -1);

        if ((uargs.exit_signal & ~csignal) || uargs.exit_signal > 64 /* _NSIG */)
		    return -EINVAL;

        if ((uargs.flags & clone_into_cgroup) && (uargs.cgroup > std::numeric_limits<int>::max() || size < sizeof(clone_args)))
		    return -EINVAL;

        kclone_args kargs
        {
            .flags = uargs.flags,
            .pidfd = reinterpret_cast<int __user *>(uargs.pidfd),
            .child_tid = reinterpret_cast<int __user *>(uargs.child_tid),
            .parent_tid = reinterpret_cast<int __user *>(uargs.parent_tid),
            .exit_signal = static_cast<int>(uargs.exit_signal),
            .stack = uargs.stack,
            .stack_size = uargs.stack_size,
            .tls = uargs.tls,
            .set_tid = set_tid,
            .set_tid_size = uargs.set_tid_size,
            .cgroup = static_cast<int>(uargs.cgroup),
        };

        const auto uset_tid = reinterpret_cast<int __user *>(uargs.set_tid);
        const auto uset_tid_size_bytes = uargs.set_tid_size * sizeof(pid_t);
        if (uargs.set_tid && !lib::copy_from_user(set_tid, uset_tid, uset_tid_size_bytes))
            return (errno = EFAULT, -1);

        return kclone(kargs);
    }

    pid_t fork()
    {
        kclone_args args { };
        // TODO
        // args.exit_signal = sigchld;
        return kclone(args);
    }

    pid_t vfork()
    {
        kclone_args args { };
        args.flags = clone_vfork | clone_vm;
        // TODO
        // args.exit_signal = sigchld;
        return kclone(args);
    }

    [[noreturn]] void exit_group(int status)
    {
        // TODO
        lib::unused(status);
        lib::panic("todo: exit_group");
        std::unreachable();
    }
} // namespace syscall::proc