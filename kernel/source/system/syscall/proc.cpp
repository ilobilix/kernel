// Copyright (C) 2024-2026  ilobilo

module system.syscall.proc;

import system.syscall.vfs;
import system.vfs;
import system.sched;
import system.memory.virt;
import system.cpu.regs;
import system.cpu.arch;
import system.bin.exec;
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

    uid_t geteuid()
    {
        return sched::current_process()->cred->euid;
    }

    gid_t getgid()
    {
        return sched::current_process()->cred->rgid;
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
                return (errno = EFAULT, -1);
        }
        if (euid)
        {
            if (!lib::copy_to_user(euid, &cred->euid, sizeof(uid_t)))
                return (errno = EFAULT, -1);
        }
        if (suid)
        {
            if (!lib::copy_to_user(suid, &cred->suid, sizeof(uid_t)))
                return (errno = EFAULT, -1);
        }
        return 0;
    }

    int getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid)
    {
        auto proc = sched::current_process();
        const auto &cred = proc->cred;
        if (rgid)
        {
            if (!lib::copy_to_user(rgid, &cred->rgid, sizeof(gid_t)))
                return (errno = EFAULT, -1);
        }
        if (egid)
        {
            if (!lib::copy_to_user(egid, &cred->egid, sizeof(gid_t)))
                return (errno = EFAULT, -1);
        }
        if (sgid)
        {
            if (!lib::copy_to_user(sgid, &cred->sgid, sizeof(gid_t)))
                return (errno = EFAULT, -1);
        }
        return 0;
    }

    pid_t getpgid(pid_t pid)
    {
        if (pid < 0)
            return (errno = EINVAL, -1);

        if (pid == 0)
            return sched::current_process()->group->pgid;

        const auto target = sched::get_process(pid);
        if (!target)
            return (errno = ESRCH, -1);

        return target->group->pgid;
    }

    int setpgid(pid_t pid, pid_t pgid)
    {
        return sched::setpgid(pid, pgid);
    }

    pid_t getsid(pid_t pid)
    {
        if (pid < 0)
            return (errno = EINVAL, -1);

        if (pid == 0)
            return sched::current_process()->session->sid;

        const auto target = sched::get_process(pid);
        if (!target)
            return (errno = ESRCH, -1);

        return target->session->sid;
    }

    pid_t setsid()
    {
        return sched::setsid();
    }

    int setfsuid(uid_t fsuid)
    {
        // TODO
        lib::unused(fsuid);
        return (errno = ENOSYS, -1);
    }

    int setfsgid(gid_t fsgid)
    {
        // TODO
        lib::unused(fsgid);
        return (errno = ENOSYS, -1);
    }

    int getgroups(int size, gid_t __user *list)
    {
        if (size < 0)
            return (errno = EINVAL, -1);

        auto uspan = lib::maybe_uspan<gid_t>::create(list, size);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = sched::getgroups(*uspan);
        if (!ret)
            return (errno = lib::map_error(ret.error()), -1);
        return *ret;
    }

    #define NGROUPS_MAX 65536
    int setgroups(std::size_t size, const gid_t __user *list)
    {
        if (size > NGROUPS_MAX)
            return (errno = EINVAL, -1);

        auto uspan = lib::maybe_uspan<gid_t>::create(list, size);
        if (!uspan.has_value())
            return (errno = EFAULT, -1);

        const auto ret = sched::setgroups(*uspan);
        if (!ret)
            return (errno = lib::map_error(ret.error()), -1);
        return 0;
    }

    int set_tid_address(int __user *tidptr)
    {
        const auto thread = sched::current_thread();
        thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(tidptr);
        return thread->tid;
    }

    mode_t umask(mode_t mask)
    {
        const auto proc = sched::current_process();
        const auto ret = proc->vfs->umask;
        proc->vfs->umask = mask & 0777;
        return ret;
    }

    int sigaction(int signum, const struct sigaction __user *act, struct sigaction __user *oldact)
    {
        // TODO
        lib::unused(signum, act, oldact);
        return (errno = ENOSYS, -1);
    }

    int sigprocmask(
        int how, const struct sigset_t __user *set,
        struct sigset_t __user *oldset, std::size_t sigsetsize
    )
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

    long futex(
        std::uint32_t __user *uaddr, int futex_op, std::uint32_t val,
        const timespec __user *timeout, std::uint32_t __user *uaddr2, std::uint32_t val3
    )
    {
        // TODO
        lib::unused(uaddr, futex_op, val, timeout, uaddr2, val3);
        // return (errno = ENOSYS, -1);
        return 0;
    }

    long get_robust_list(
        int pid, struct robust_list_head __user *__user *head_ptr,
        std::size_t __user *sizep
    )
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

    int prlimit(
        pid_t pid, int resource, const struct rlimit __user *new_limit,
        struct rlimit __user *old_limit
    )
    {
        // TODO
        lib::unused(pid, resource, new_limit, old_limit);
        return (errno = ENOSYS, -1);
    }

    using enum sched::clone_flags;

    long clone(
        unsigned long flags, void __user *stack, int __user *parent_tid,
        int __user *child_tid, unsigned long tls
    )
    {
        const auto kflags = (flags & 0xFFFFFFFF) & ~csignal;
        if ((kflags & clone_pidfd) && (kflags & clone_parent_settid))
            return (errno = EINVAL, -1);

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
            return (errno = EINVAL, -1);

        if (size > sizeof(clone_args))
            return (errno = E2BIG, -1);

        if (!lib::copy_from_user(&uargs, cl_args, size))
            return (errno = EFAULT, -1);

        if (uargs.set_tid_size > 32)
            return (errno = EINVAL, -1);

        if (!uargs.set_tid && uargs.set_tid_size > 0)
            return (errno = EINVAL, -1);

        if (uargs.set_tid && uargs.set_tid_size == 0)
            return (errno = EINVAL, -1);

        if ((uargs.exit_signal & ~csignal) || uargs.exit_signal > 64 /* _NSIG */)
            return (errno = EINVAL, -1);

        constexpr auto max = std::numeric_limits<int>::max();
        constexpr std::size_t size_v2 = 88;
        if ((uargs.flags & clone_into_cgroup) && (uargs.cgroup > max || size < size_v2))
            return (errno = EINVAL, -1);

        if (uargs.flags & clone_detached)
            return (errno = EINVAL, -1);

        if ((uargs.flags & (clone_thread | clone_parent)) && uargs.exit_signal)
            return (errno = EINVAL, -1);

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
            return (errno = EFAULT, -1);

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
        // TODO-SCHED-REWRITE
        return (errno = ENOSYS, -1);
        // using namespace vfs;

        // if (flags & ~(at_symlink_nofollow | at_empty_path))
        //     return (errno = EINVAL, -1);

        // const auto thread = sched::this_thread();
        // const auto proc = thread->parent;

        // const bool follow_links = (flags & at_symlink_nofollow) == 0;
        // const bool empty_path = (flags & at_empty_path) != 0;

        // const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        // if (!target.has_value())
        //     return -1;

        // const auto get_array = [](auto &vec, auto *uarray)
        // {
        //     if (!uarray)
        //         return true;

        //     std::size_t idx = 0;
        //     while (true)
        //     {
        //         std::uintptr_t addr;
        //         if (!lib::copy_from_user(&addr, uarray + idx, sizeof(addr)))
        //             return false;

        //         if (addr == 0)
        //             break;

        //         const auto ptr = reinterpret_cast<const char __user *>(addr);
        //         auto str = lib::user_string::get(ptr);
        //         if (!str.has_value())
        //             return false;
        //         vec.emplace_back(std::move(*str));
        //         idx++;
        //     }
        //     return true;
        // };

        // std::vector<std::string> kargv;
        // if (!get_array(kargv, argv))
        //     return (errno = EFAULT, -1);

        // std::vector<std::string> kenvp;
        // if (!get_array(kenvp, envp))
        //     return (errno = EFAULT, -1);

        // const auto stat = target->dentry->inode->stat;
        // const auto is_suid = (stat.st_mode & s_isuid) != 0;
        // const auto is_sgid = (stat.st_mode & s_isgid) != 0;

        // auto file = vfs::file::create(target.value(), 0, 0, 0);
        // if (!file)
        //     return (errno = EACCES, -1);

        // auto format = bin::exec::identify(file);
        // if (!format)
        //     return (errno = ENOEXEC, -1);

        // std::string kpathname;
        // if (pathname)
        // {
        //     auto ret = lib::user_string::get(pathname);
        //     if (!ret.has_value())
        //         return (errno = EFAULT, -1);
        //     kpathname = std::move(*ret);
        // }

        // {
        //     const std::unique_lock _ { proc->lock };
        //     for (auto &[tid, thrd] : proc->threads)
        //     {
        //         if (thrd == thread)
        //             continue;

        //         thrd->og_ustack_top = 0;
        //         thrd->kill();
        //     }
        // }

        // while (true)
        // {
        //     bool alone = false;
        //     {
        //         const std::unique_lock _ { proc->lock };
        //         alone = true;
        //         for (auto &[tid, thrd] : proc->threads)
        //         {
        //             if (thrd == thread)
        //                 continue;

        //             if (thrd->status != sched::status::killed)
        //             {
        //                 alone = false;
        //                 break;
        //             }
        //         }
        //     }
        //     if (alone)
        //     {
        //         sched::yield();
        //         break;
        //     }
        //     sched::yield();
        // }

        // auto pmap = std::make_shared<vmm::pagemap>();
        // if (!pmap)
        //     return (errno = ENOMEM, -1);

        // auto new_vmspace = std::make_shared<vmm::vmspace>(std::move(pmap));
        // if (!new_vmspace)
        //     return (errno = ENOMEM, -1);

        // auto old_vmspace = proc->vmspace;
        // auto old_stack_top = proc->next_stack_top;

        // proc->vmspace = new_vmspace;
        // proc->next_stack_top = vmm::vmspace::vspace_top;

        // auto new_thread = format->load({
        //     .pathname = std::move(kpathname),
        //     .file = file,
        //     .interp = { },
        //     .argv = std::move(kargv),
        //     .envp = std::move(kenvp),
        // }, proc);

        // if (!new_thread)
        // {
        //     proc->vmspace = old_vmspace;
        //     proc->next_stack_top = old_stack_top;
        //     return (errno = ENOEXEC, -1);
        // }

        // arch::int_switch(false);
        // {
        //     const std::unique_lock _ { proc->lock };

        //     proc->threads.erase(new_thread->tid);
        //     proc->threads.erase(thread->tid);

        //     new_thread->tid = proc->pid;
        //     proc->threads[new_thread->tid] = new_thread;
        // }

        // if (is_suid)
        //     proc->euid = stat.st_uid;
        // if (is_sgid)
        //     proc->egid = stat.st_gid;

        // auto new_fdt = std::make_shared<sched::process::fdtable>(*proc->fdt);
        // proc->fdt = new_fdt;
        // proc->fdt->close_on_exec();

        // proc->has_execved = true;

        // new_thread->status = sched::status::ready;
        // sched::enqueue(new_thread, thread->running_on->idx);

        // thread->og_ustack_top = 0;
        // thread->tid = -1;
        // thread->status = sched::status::killed;

        // sched::yield();
        // std::unreachable();
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
            return (errno = EFAULT, -1);

        return ret;
    }

    [[noreturn]] void exit_group(int status)
    {
        sched::process_exit(status);
        std::unreachable();
    }

    // TODO: stub
    int tgkill(pid_t tgid, pid_t tid, int sig)
    {
        // TODO-SCHED-REWRITE
        return (errno = ENOSYS, -1);
        // if (tgid <= 0 || tid <= 0)
        //     return (errno = EINVAL, -1);

        // const auto proc = sched::proc_for(tgid);
        // if (!proc)
        //     return (errno = ESRCH, -1);

        // const auto current = sched::this_thread();

        // constexpr auto is_fatal = [](int sig)
        // {
        //     switch (sig)
        //     {
        //         case 1: case 2: case 3: case 4: case 6:
        //         case 8: case 9: case 11: case 13: case 14: case 15:
        //             return true;
        //         default:
        //             return false;
        //     }
        // };

        // if (sig == 0)
        //     return 0;

        // {
        //     const std::unique_lock _ { proc->lock };
        //     if (!proc->threads.contains(tid))
        //         return (errno = ESRCH, -1);
        // }

        // if (is_fatal(sig) && tgid == current->parent->pid && tid == current->tid)
        // {
        //     sched::exit(128 + sig);
        //     std::unreachable();
        // }

        // // TODO: queue signals
        // return (errno = ENOSYS, -1);
    }
} // namespace syscall::proc
