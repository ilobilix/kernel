// Copyright (C) 2024-2026  ilobilo

module system.syscall.proc;

import system.syscall.vfs;
import system.vfs;
import system.scheduler;
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

    pid_t getpgrp()
    {
        return sched::this_thread()->parent->pgid;
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

        if (target->pgid == pgid)
            return 0;

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
        if (!target_group)
        {
            if (pgid == pid)
            {
                target_group = sched::create_group(pgid);
                target_group->sid = target->sid;
                auto sess = sched::session_for(target->sid);
                if (sess)
                    sess->members.write_lock().value()[pgid] = target_group;
            }
            else return (errno = EPERM, -1);
        }
        else if (target_group->sid != target->sid)
            return (errno = EPERM, -1);

        if (!sched::change_group(target, target_group))
            return (errno = EINVAL, -1);
        return (errno = no_error, 0);
    }

    pid_t setsid()
    {
        const auto proc = sched::this_thread()->parent;

        if (proc->pid == proc->pgid)
            return (errno = EPERM, -1);

        const auto grp = sched::create_group(proc->pid);
        const auto sess = sched::create_session(proc->pid);

        if (!sched::change_session(grp, sess))
            return (errno = ENOSYS, -1);

        if (!sched::change_group(proc, grp))
            return (errno = ENOSYS, -1);

        return proc->sid;
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
        const auto thread = sched::this_thread();
        thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(tidptr);
        return thread->tid;
    }

    mode_t umask(mode_t mask)
    {
        const auto proc = sched::this_thread()->parent;
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

    namespace
    {
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

        pid_t kclone(const kclone_args &args)
        {
            const auto caller = sched::this_thread();
            const auto old_proc = caller->parent;
            const auto flags = args.flags;

            if ((flags & (clone_child_settid | clone_child_cleartid)) && !args.child_tid)
                return (errno = EFAULT, -1);

            if ((flags & clone_parent_settid) && !args.parent_tid)
                return (errno = EFAULT, -1);

            if ((flags & clone_clear_sighand) && (flags & clone_sighand))
                return (errno = EINVAL, -1);

            if ((flags & clone_thread) && !(flags & clone_sighand))
                return (errno = EINVAL, -1);

            if ((flags & clone_thread) && args.exit_signal)
                return (errno = EINVAL, -1);

            if ((flags & clone_thread) && (flags & clone_pidfd))
                return (errno = EINVAL, -1);

            if (!(flags & clone_vm) && (flags & clone_sighand))
                return (errno = EINVAL, -1);

            if ((flags & clone_fs) && (flags & clone_newns))
                return (errno = EINVAL, -1);

            if ((flags & clone_fs) && (flags & clone_newuser))
                return (errno = EINVAL, -1);

            if ((flags & clone_newipc) && (flags & clone_sysvsem))
                return (errno = EINVAL, -1);

            if ((flags & clone_newpid) && (flags & (clone_thread | clone_parent)))
                return (errno = EINVAL, -1);

            if ((flags & clone_newuser) && (flags & clone_thread))
                return (errno = EINVAL, -1);

            // TODO: namespaces
            if ((flags & (clone_newipc | clone_newnet | clone_newpid | clone_newuser | clone_newuts)))
                return (errno = EINVAL, -1);

            if ((flags & clone_parent) && old_proc->pid == 1)
                return (errno = EINVAL, -1);

            // TODO
            // const auto needs_priv = flags & (clone_newcgroup | clone_newipc | clone_newnet |
            //     clone_newns | clone_newpid | clone_newuts);
            // if (needs_priv && !cap_sys_admin)
            //     return (errno = EPERM, -1);

            sched::process *new_proc;
            if (!(flags & clone_thread))
            {
                new_proc = new sched::process { };
                new_proc->pid = sched::alloc_pid(new_proc);

                if (flags & clone_parent)
                    new_proc->parent = old_proc->parent;
                else
                    new_proc->parent = old_proc;

                new_proc->pgid = new_proc->parent->pgid;
                new_proc->sid = new_proc->parent->sid;
                {
                    auto grp = sched::group_for(new_proc->pgid);
                    auto wlocked = grp->members.write_lock();
                    lib::bug_on(wlocked->contains(new_proc->pid));
                    wlocked.value()[new_proc->pid] = new_proc;
                }

                new_proc->rgid = old_proc->rgid;
                new_proc->sgid = old_proc->sgid;
                new_proc->egid = old_proc->egid;

                new_proc->ruid = old_proc->ruid;
                new_proc->suid = old_proc->suid;
                new_proc->euid = old_proc->euid;

                new_proc->supplementary_gids.write_lock() =
                    *old_proc->supplementary_gids.read_lock();

                if (flags & clone_vm)
                {
                    if (!(flags & clone_vfork))
                    { } // TODO: clear alternate signal stacks

                    new_proc->vmspace = old_proc->vmspace;
                }
                else
                {
                    auto pmap = std::make_shared<vmm::pagemap>();
                    if (!pmap)
                        return (errno = ENOMEM, -1);
                    auto ret = old_proc->vmspace->fork(pmap);
                    if (!ret)
                        return (errno = ENOMEM, -1);
                    new_proc->vmspace = std::move(*ret);
                    new_proc->next_stack_top = old_proc->next_stack_top;
                }

                if (flags & clone_fs)
                    new_proc->vfs = old_proc->vfs;
                else
                    new_proc->vfs = std::make_shared<sched::process::vfs_state>(*old_proc->vfs);

                if (flags & clone_files)
                    new_proc->fdt = old_proc->fdt;
                else
                    new_proc->fdt = std::make_shared<sched::process::fdtable>(*old_proc->fdt);
            }
            else new_proc = old_proc;

            // TODO: this is so bad I want to nuke it

            auto new_thread = sched::thread::create(new_proc, 0, 0, true, true);
            std::memcpy(
                &new_thread->regs,
                caller->saved_regs,
                sizeof(cpu::registers)
            );
            auto &regs = new_thread->regs;

            const auto cleanup = [&] {
                if (new_proc == old_proc)
                {
                    const std::unique_lock _ { old_proc->lock };
                    old_proc->threads.erase(new_thread->tid);;
                    delete new_thread;
                    return;
                }

                delete new_thread;
                delete new_proc;
            };

            constexpr std::uint64_t stack_alignment = 16;
            if (args.stack != 0)
            {
                std::uintptr_t req = static_cast<std::uintptr_t>(args.stack);

                if (args.stack_size != 0)
                {
                    constexpr auto max = std::numeric_limits<std::uintptr_t>::max();
                    if (req > max - static_cast<std::uintptr_t>(args.stack_size))
                    {
                        cleanup();
                        return (errno = EINVAL, -1);
                    }

                    req += static_cast<std::uintptr_t>(args.stack_size);
                }

                if (req & (stack_alignment - 1))
                {
                    cleanup();
                    return (errno = EINVAL, -1);
                }

                new_thread->ustack_top = req;
#if defined(__x86_64__)
                regs.rsp = req;
#endif
            }
            else
            {
                new_thread->ustack_top = caller->ustack_top;
                if (!(flags & clone_vm))
                    new_thread->og_ustack_top = 0;
                else
                    new_thread->og_ustack_top = caller->og_ustack_top;
            }

#if defined(__x86_64__)
            if (flags & clone_settls)
                new_thread->fs_base = args.tls;
            else
                new_thread->fs_base = cpu::fs::read();
            new_thread->gs_base = cpu::gs::read_kernel();

            regs.rax = 0;
#else
            lib::unused(regs);
            lib::panic("kclone: unsupported architecture");
            std::unreachable();
#endif

            // TODO: set_tid

            if (flags & clone_child_settid)
            {
                new_thread->set_child_tid = reinterpret_cast<std::uintptr_t>(args.child_tid);
                // if (!lib::copy_to_user(args.child_tid, &new_thread->tid, sizeof(pid_t)))
                // {
                //     cleanup();
                //     return (errno = EFAULT, -1);
                // }
            }

            if (flags & clone_child_cleartid)
            {
                new_thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(args.child_tid);
                // TODO: wake up futex at that address
            }

            if (flags & clone_parent_settid)
            {
                if (!lib::copy_to_user(args.parent_tid, &new_thread->tid, sizeof(pid_t)))
                {
                    cleanup();
                    return (errno = EFAULT, -1);
                }
            }

            if (flags & clone_clear_sighand)
            {
                // TODO: use default signals
            }
            else { } // TODO: copy parent signals

            if (flags & clone_sighand)
            {
                // TODO: share signal handlers
            }

            // TODO: cgroups
            // TODO: namespaces
            // TODO: pidfd

            if (flags & clone_vfork)
            {
                // TODO: suspend caller until child calls execve or exit
            }

            if (!(flags & clone_thread))
            {
                const std::unique_lock _ { new_proc->parent->lock };
                new_proc->parent->children[new_proc->pid] = new_proc;
            }

            new_thread->status = sched::status::ready;
            new_thread->vruntime = caller->vruntime;
            new_thread->schedule_time = caller->schedule_time;
            sched::enqueue(new_thread, caller->running_on->idx);

            return new_thread->tid;
        }
    } // namespace

    long clone(
        unsigned long flags, void __user *stack, int __user *parent_tid,
        int __user *child_tid, unsigned long tls
    )
    {
        const auto kflags = (flags & 0xFFFFFFFF) & ~csignal;
        if ((kflags & clone_pidfd) && (kflags & clone_parent_settid))
            return (errno = EINVAL, -1);

        return kclone({
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
            return (errno = EINVAL, -1);

        constexpr auto max = std::numeric_limits<int>::max();
        if ((uargs.flags & clone_into_cgroup) && (uargs.cgroup > max || size < sizeof(clone_args)))
            return (errno = EINVAL, -1);

        if (uargs.flags & clone_detached)
            return (errno = EINVAL, -1);

        if ((uargs.flags & (clone_thread | clone_parent)) && uargs.exit_signal)
            return (errno = EINVAL, -1);

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

    int execveat(
        int dirfd, const char __user *pathname, const char __user *const __user *argv,
        const char __user *const __user *envp, int flags
    )
    {
        using namespace vfs;

        if (flags & ~(at_symlink_nofollow | at_empty_path))
            return (errno = EINVAL, -1);

        const auto thread = sched::this_thread();
        const auto proc = thread->parent;

        const bool follow_links = (flags & at_symlink_nofollow) == 0;
        const bool empty_path = (flags & at_empty_path) != 0;

        const auto target = get_target(proc, dirfd, pathname, follow_links, empty_path, true);
        if (!target.has_value())
            return -1;

        const auto get_array = [](auto &vec, auto *uarray)
        {
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
            return (errno = EFAULT, -1);

        std::vector<std::string> kenvp;
        if (!get_array(kenvp, envp))
            return (errno = EFAULT, -1);

        const auto stat = target->dentry->inode->stat;
        const auto is_suid = (stat.st_mode & s_isuid) != 0;
        const auto is_sgid = (stat.st_mode & s_isgid) != 0;

        auto file = vfs::file::create(target.value(), 0, 0, 0);
        if (!file)
            return (errno = EACCES, -1);

        auto format = bin::exec::identify(file);
        if (!format)
            return (errno = ENOEXEC, -1);

        std::string kpathname;
        if (pathname)
        {
            auto ret = lib::user_string::get(pathname);
            if (!ret.has_value())
                return (errno = EFAULT, -1);
            kpathname = std::move(*ret);
        }

        {
            const std::unique_lock _ { proc->lock };
            for (auto &[tid, thrd] : proc->threads)
            {
                if (thrd == thread)
                    continue;

                thrd->og_ustack_top = 0;
                thrd->kill();
            }
        }

        while (true)
        {
            bool alone = false;
            {
                const std::unique_lock _ { proc->lock };
                alone = true;
                for (auto &[tid, thrd] : proc->threads)
                {
                    if (thrd == thread)
                        continue;

                    if (thrd->status != sched::status::killed)
                    {
                        alone = false;
                        break;
                    }
                }
            }
            if (alone)
            {
                sched::yield();
                break;
            }
            sched::yield();
        }

        auto pmap = std::make_shared<vmm::pagemap>();
        if (!pmap)
            return (errno = ENOMEM, -1);

        auto new_vmspace = std::make_shared<vmm::vmspace>(std::move(pmap));
        if (!new_vmspace)
            return (errno = ENOMEM, -1);

        auto old_vmspace = proc->vmspace;
        auto old_stack_top = proc->next_stack_top;

        proc->vmspace = new_vmspace;
        proc->next_stack_top = vmm::vmspace::vspace_top;

        auto new_thread = format->load({
            .pathname = std::move(kpathname),
            .file = file,
            .interp = { },
            .argv = std::move(kargv),
            .envp = std::move(kenvp),
        }, proc);

        if (!new_thread)
        {
            proc->vmspace = old_vmspace;
            proc->next_stack_top = old_stack_top;
            return (errno = ENOEXEC, -1);
        }

        arch::int_switch(false);
        {
            const std::unique_lock _ { proc->lock };

            proc->threads.erase(new_thread->tid);
            proc->threads.erase(thread->tid);

            new_thread->tid = proc->pid;
            proc->threads[new_thread->tid] = new_thread;
        }

        if (is_suid)
            proc->euid = stat.st_uid;
        if (is_sgid)
            proc->egid = stat.st_gid;

        auto new_fdt = std::make_shared<sched::process::fdtable>(*proc->fdt);
        proc->fdt = new_fdt;
        proc->fdt->close_on_exec();

        proc->has_execved = true;

        new_thread->status = sched::status::ready;
        sched::enqueue(new_thread, thread->running_on->idx);

        thread->og_ustack_top = 0;
        thread->tid = -1;
        thread->status = sched::status::killed;

        sched::yield();
        std::unreachable();
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

        enum {
            wnohang = 1,
            wuntraced = 2,
            wcontinued = 8
        };

        const auto current = sched::this_thread();
        const auto proc = current->parent;

        while (true)
        {
            sched::process *found = nullptr;
            pid_t found_pid = -1;

            {
                const std::unique_lock _ { proc->lock };

                if (proc->children.empty())
                    return (errno = ECHILD, -1);

                bool any_match = false;
                for (auto &[cpid, child] : proc->children)
                {
                    bool matches = false;
                    if (pid == -1)
                        matches = true;
                    else if (pid > 0)
                        matches = (cpid == pid);
                    else if (pid == 0)
                        matches = (child->pgid == proc->pgid);
                    else
                        matches = (child->pgid == -pid);

                    if (!matches)
                        continue;

                    any_match = true;

                    if (child->exited && child->threads.empty())
                    {
                        found = child;
                        found_pid = cpid;
                        break;
                    }

                    // TODO: stopped/continued checks for WUNTRACED/WCONTINUED
                }

                if (!any_match)
                    return (errno = ECHILD, -1);
            }

            if (found)
            {
                const auto status = found->exit_status;
                const auto wstatus_val = (status & 0xFF) << 8;

                if (wstatus)
                {
                    if (!lib::copy_to_user(wstatus, &wstatus_val, sizeof(int)))
                        return (errno = EFAULT, -1);
                }

                {
                    const std::unique_lock _ { proc->lock };
                    proc->children.erase(found_pid);
                }

                bool should_delete = false;
                {
                    const std::unique_lock _ { found->lock };
                    found->reaped = true;
                    should_delete = found->threads.empty();
                }

                if (should_delete)
                    delete found;

                return found_pid;
            }

            if (options & wnohang)
                return 0;

            proc->waiter.wait();
        }
    }

    [[noreturn]] void exit_group(int status)
    {
        sched::exit(status);
        std::unreachable();
    }

    // TODO: stub
    int tgkill(pid_t tgid, pid_t tid, int sig)
    {
        if (tgid <= 0 || tid <= 0)
            return (errno = EINVAL, -1);

        const auto proc = sched::proc_for(tgid);
        if (!proc)
            return (errno = ESRCH, -1);

        const auto current = sched::this_thread();

        constexpr auto is_fatal = [](int sig)
        {
            switch (sig)
            {
                case 1: case 2: case 3: case 4: case 6:
                case 8: case 9: case 11: case 13: case 14: case 15:
                    return true;
                default:
                    return false;
            }
        };

        if (sig == 0)
            return 0;

        {
            const std::unique_lock _ { proc->lock };
            if (!proc->threads.contains(tid))
                return (errno = ESRCH, -1);
        }

        if (is_fatal(sig) && tgid == current->parent->pid && tid == current->tid)
        {
            sched::exit(128 + sig);
            std::unreachable();
        }

        // TODO: queue signals
        return (errno = ENOSYS, -1);
    }
} // namespace syscall::proc
