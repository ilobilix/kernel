// Copyright (C) 2024-2026  ilobilo

module system.syscall.proc;

import system.syscall.vfs;
import system.vfs;
import system.sched;
import system.memory.virt;
import system.cpu.regs;
import system.cpu.arch;
import magic_enum;
import arch;
import lib;
import std;

namespace syscall::proc
{
    pid_t gettid()
    {
        return sched::current_thread()->tid;
    }

    pid_t getpid()
    {
        return sched::current_process()->pid;
    }

    pid_t getppid()
    {
        const auto parent = sched::current_process()->parent;
        return parent ? parent->pid : 0;
    }

    pid_t getpgrp()
    {
        return sched::current_process()->group->pgid;
    }

    uid_t getuid()
    {
        return sched::current_process()->cred->ruid;
    }

    int setuid(uid_t uid)
    {
        if (const auto ret = sched::setuid(uid); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    gid_t getgid()
    {
        return sched::current_process()->cred->rgid;
    }

    int setgid(gid_t gid)
    {
        if (const auto ret = sched::setgid(gid); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    uid_t geteuid()
    {
        return sched::current_process()->cred->euid;
    }

    gid_t getegid()
    {
        return sched::current_process()->cred->egid;
    }

    int getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid)
    {
        auto proc = sched::current_process();
        const auto &cred = proc->cred;
        if (ruid)
        {
            if (!lib::copy_to_user(ruid, &cred->ruid, sizeof(uid_t)))
                return -EFAULT;
        }
        if (euid)
        {
            if (!lib::copy_to_user(euid, &cred->euid, sizeof(uid_t)))
                return -EFAULT;
        }
        if (suid)
        {
            if (!lib::copy_to_user(suid, &cred->suid, sizeof(uid_t)))
                return -EFAULT;
        }
        return 0;
    }

    int setresuid(uid_t ruid, uid_t euid, uid_t suid)
    {
        if (const auto ret = sched::setresuid(ruid, euid, suid); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid)
    {
        auto proc = sched::current_process();
        const auto &cred = proc->cred;
        if (rgid)
        {
            if (!lib::copy_to_user(rgid, &cred->rgid, sizeof(gid_t)))
                return -EFAULT;
        }
        if (egid)
        {
            if (!lib::copy_to_user(egid, &cred->egid, sizeof(gid_t)))
                return -EFAULT;
        }
        if (sgid)
        {
            if (!lib::copy_to_user(sgid, &cred->sgid, sizeof(gid_t)))
                return -EFAULT;
        }
        return 0;
    }

    int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
    {
        if (const auto ret = sched::setresgid(rgid, egid, sgid); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    pid_t getpgid(pid_t pid)
    {
        if (pid < 0)
            return -EINVAL;

        if (pid == 0)
            return sched::current_process()->group->pgid;

        const auto target = sched::get_process(pid);
        if (!target)
            return -ESRCH;

        return target->group->pgid;
    }

    int setpgid(pid_t pid, pid_t pgid)
    {
        return sched::setpgid(pid, pgid);
    }

    pid_t getsid(pid_t pid)
    {
        if (pid < 0)
            return -EINVAL;

        if (pid == 0)
            return sched::current_process()->session->sid;

        const auto target = sched::get_process(pid);
        if (!target)
            return -ESRCH;

        return target->session->sid;
    }

    pid_t setsid()
    {
        return sched::setsid();
    }

    int setfsuid(uid_t fsuid)
    {
        return sched::setfsuid(fsuid);
    }

    int setfsgid(gid_t fsgid)
    {
        return sched::setfsgid(fsgid);
    }

    int getgroups(int size, gid_t __user *list)
    {
        if (size < 0)
            return -EINVAL;

        auto uspan = lib::maybe_uspan<gid_t>::create(list, size);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = sched::getgroups(*uspan);
        if (!ret)
            return -lib::map_error(ret.error());
        return *ret;
    }

    #define NGROUPS_MAX 65536
    int setgroups(std::size_t size, const gid_t __user *list)
    {
        if (size > NGROUPS_MAX)
            return -EINVAL;

        auto uspan = lib::maybe_uspan<gid_t>::create(list, size);
        if (!uspan.has_value())
            return -EFAULT;

        const auto ret = sched::setgroups(*uspan);
        if (!ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int set_tid_address(int __user *tidptr)
    {
        const auto thread = sched::current_thread();
        thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(tidptr);
        return thread->tid;
    }

    int kill(pid_t pid, int sig)
    {
        return sched::kill(pid, sig);
    }

    int tgkill(pid_t tgid, pid_t tid, int sig)
    {
        using namespace sched;

        if (sig < 0 || sig > static_cast<int>(nsig))
            return -EINVAL;

        if (tgid <= 0 || tid <= 0)
            return -EINVAL;

        const auto target_proc = get_process(tgid);
        if (!target_proc)
            return -ESRCH;

        thread_t *target_thread = nullptr;
        {
            auto locked = target_proc->threads.lock();
            auto it = locked->find(tid);
            if (it != locked->end())
                target_thread = it->second;
        }
        if (!target_thread)
            return -ESRCH;

        if (!check_kill(sig, target_proc))
            return -EPERM;

        if (sig == 0)
            return 0;

        const auto caller = current_process();
        siginfo_t info {
            .signo = sig,
            .code = si_tkill,
            .err = 0,
            .pid = caller->pid,
            .uid = caller->cred->ruid,
            .status = 0,
            .addr = 0,
            .value = 0,
        };
        return send_signal(target_thread, info) ? 0 : -ESRCH;
    }

    int rt_sigaction(
        int signum, const sched::sigaction_t __user *act,
        sched::sigaction_t __user *oldact, std::size_t sigsetsize
    )
    {
        using namespace sched;

        if (sigsetsize != sizeof(sigset_t))
            return -EINVAL;

        if (signum < 1 || signum > static_cast<int>(nsig))
            return -EINVAL;

        if (signum == sigkill || signum == sigstop)
            return -EINVAL;

        sigaction_t newact { };
        if (act)
        {
            if (!lib::copy_from_user(&newact, act, sizeof(sigaction_t)))
                return -EFAULT;
            newact.mask &= ~sigmask_uncatchable;
        }

        sigaction_t old;
        {
            auto &sigacts = current_process()->sigactions;
            const std::unique_lock _ { sigacts->lock };
            old = sigacts->actions[signum - 1];
            if (act)
                sigacts->actions[signum - 1] = newact;
        }

        if (oldact && !lib::copy_to_user(oldact, &old, sizeof(sigaction_t)))
            return -EFAULT;

        return 0;
    }

    int rt_sigprocmask(
        int how, const sched::sigset_t __user *set,
        sched::sigset_t __user *oldset, std::size_t sigsetsize
    )
    {
        using namespace sched;

        if (sigsetsize != sizeof(sigset_t))
            return -EINVAL;

        if (how < sig_block || how > sig_setmask)
            return -EINVAL;

        sigset_t kset;
        if (set)
        {
            if (!lib::copy_from_user(&kset, set, sizeof(sigset_t)))
                return -EFAULT;
            kset &= ~sigmask_uncatchable;
        }

        auto &sigmask = current_thread()->sigmask;
        const sigset_t old = sigmask;

        if (set)
        {
            switch (how)
            {
                case sig_block:
                    sigmask |= kset;
                    break;
                case sig_unblock:
                    sigmask &= ~kset;
                    break;
                case sig_setmask:
                    sigmask = kset;
                    break;
            }
        }

        if (oldset && !lib::copy_to_user(oldset, &old, sizeof(sigset_t)))
            return -EFAULT;

        return 0;
    }

    int sigaltstack(const sched::stack_t __user *ss, sched::stack_t __user *old_ss)
    {
        // TODO
        lib::unused(ss, old_ss);
        return -ENOSYS;
    }

    int rseq(struct rseq __user *rseq, std::uint32_t rseq_len, int flags, std::uint32_t sig)
    {
        // TODO
        lib::unused(rseq, rseq_len, flags, sig);
        return -ENOSYS;
    }

    long futex(
        std::uint32_t __user *uaddr, int futex_op, std::uint32_t val,
        const timespec __user *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3
    )
    {
        // TODO
        lib::unused(uaddr, futex_op, val, timeout, uaddr2, val3);
        // return -ENOSYS;
        return 0;
    }

    long get_robust_list(
        int pid, struct robust_list_head __user *__user *head_ptr,
        std::size_t __user *sizep
    )
    {
        // TODO
        lib::unused(pid, head_ptr, sizep);
        return -ENOSYS;
    }

    long set_robust_list(struct robust_list_head __user *head, std::size_t size)
    {
        // TODO
        lib::unused(head, size);
        return -ENOSYS;
    }

    struct rlimit
    {
        rlim_t rlim_cur;
        rlim_t rlim_max;
    };

    int prlimit(
        pid_t pid, int resource, const struct rlimit __user *new_limit,
        struct rlimit __user *old_limit
    )
    {
        // TODO
        lib::unused(pid, resource, new_limit, old_limit);
        return -ENOSYS;
    }

    using enum sched::clone_flags;

    long clone(
        unsigned long flags, void __user *stack, int __user *parent_tid,
        int __user *child_tid, unsigned long tls
    )
    {
        const auto kflags = (flags & 0xFFFFFFFF) & ~csignal;
        if ((kflags & clone_pidfd) && (kflags & clone_parent_settid))
            return -EINVAL;

        return sched::clone({
            .flags = kflags,
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
        if (size < 64)
            return -EINVAL;

        if (size > sizeof(clone_args))
            return -E2BIG;

        if (!lib::copy_from_user(&uargs, cl_args, size))
            return -EFAULT;

        if (uargs.set_tid_size > 32)
            return -EINVAL;

        if (!uargs.set_tid && uargs.set_tid_size > 0)
            return -EINVAL;

        if (uargs.set_tid && uargs.set_tid_size == 0)
            return -EINVAL;

        if ((uargs.exit_signal & ~csignal) || uargs.exit_signal > 64 /* _NSIG */)
            return -EINVAL;

        constexpr auto max = std::numeric_limits<int>::max();
        constexpr std::size_t size_v2 = 88;
        if ((uargs.flags & clone_into_cgroup) && (uargs.cgroup > max || size < size_v2))
            return -EINVAL;

        if (uargs.flags & clone_detached)
            return -EINVAL;

        if ((uargs.flags & (clone_thread | clone_parent)) && uargs.exit_signal)
            return -EINVAL;

        sched::kclone_args_t kargs
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
            return -EFAULT;

        return sched::clone(kargs);
    }

    pid_t fork()
    {
        sched::kclone_args_t args { };
        // TODO
        // args.exit_signal = sigchld;
        return sched::clone(args);
    }

    pid_t vfork()
    {
        sched::kclone_args_t args { };
        args.flags = clone_vfork | clone_vm;
        // TODO
        // args.exit_signal = sigchld;
        return sched::clone(args);
    }

    int execveat(
        int dirfd, const char __user *pathname, const char __user *const __user *argv,
        const char __user *const __user *envp, int flags
    )
    {
        using namespace vfs;

        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return -EINVAL;

        const auto proc = sched::current_process();

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -lib::map_error(target.error());

        const auto get_array = [](auto &vec, auto *uarray) {
            if (!uarray)
                return true;

            std::size_t idx = 0;
            while (true)
            {
                std::uintptr_t addr;
                if (!lib::copy_from_user(&addr, uarray + idx, sizeof(addr)))
                    return false;

                if (addr == 0)
                    break;

                const auto ptr = reinterpret_cast<const char __user *>(addr);
                auto str = lib::user_string::get(ptr);
                if (!str.has_value())
                    return false;
                vec.emplace_back(std::move(*str));
                idx++;
            }
            return true;
        };

        std::vector<std::string> kargv;
        if (!get_array(kargv, argv))
            return -EFAULT;

        std::vector<std::string> kenvp;
        if (!get_array(kenvp, envp))
            return -EFAULT;

        std::string kpathname;
        if (pathname)
        {
            auto ret = lib::user_string::get(pathname);
            if (!ret.has_value())
                return -EFAULT;
            kpathname = std::move(*ret);
        }

        return sched::exec(
            std::move(*target),
            std::move(kargv), std::move(kenvp),
            std::move(kpathname)
        );
    }

    int execve(
        const char __user *pathname, const char __user *const __user *argv,
        const char __user *const __user *envp
    )
    {
        return execveat(vfs::at_fdcwd, pathname, argv, envp, 0);
    }

    pid_t wait4(pid_t pid, int __user *wstatus, int options, struct rusage __user *rusage)
    {
        // TODO: rusage
        lib::unused(rusage);

        int status;
        const auto ret = sched::waitpid(pid, options, &status);

        if (wstatus && !lib::copy_to_user(wstatus, &status, sizeof(int)))
            return -EFAULT;

        return ret;
    }

    [[noreturn]] void exit_group(int status)
    {
        sched::process_exit(status);
        std::unreachable();
    }
} // namespace syscall::proc
