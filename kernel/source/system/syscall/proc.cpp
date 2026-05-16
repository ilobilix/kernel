// Copyright (C) 2024-2026  ilobilo

module system.syscall.proc;

import system.syscall.vfs;
import system.vfs;
import system.memory.virt;
import system.cpu.regs;
import system.cpu.arch;
import system.cpu;
import magic_enum;
import arch;

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
        const auto parent = sched::current_process()->parent.lock();
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

    int setreuid(uid_t ruid, uid_t euid)
    {
        if (const auto ret = sched::setreuid(ruid, euid); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int setregid(gid_t rgid, gid_t egid)
    {
        if (const auto ret = sched::setregid(rgid, egid); !ret)
            return -lib::map_error(ret.error());
        return 0;
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

    int getpriority(int which, int who)
    {
        return sched::get_priority(which, who);
    }

    int setpriority(int which, int who, int prio)
    {
        return sched::set_priority(which, who, prio);
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

    namespace
    {
        constexpr std::uint32_t cap_version_1 = 0x19980330;
        constexpr std::uint32_t cap_version_2 = 0x20071026;
        constexpr std::uint32_t cap_version_3 = 0x20080522;
        constexpr std::uint32_t cap_kernel_version = cap_version_3;

        std::size_t cap_data_count(std::uint32_t version)
        {
            return version == cap_version_1 ? 1 : 2;
        }
    } // namespace

    struct cap_user_header
    {
        std::uint32_t version;
        int pid;
    };

    struct cap_user_data
    {
        std::uint32_t effective;
        std::uint32_t permitted;
        std::uint32_t inheritable;
    };

    int capget(cap_user_header __user *header, cap_user_data __user *data)
    {
        if (header == nullptr)
            return -EFAULT;

        cap_user_header hdr { };
        if (!lib::copy_from_user(&hdr, header, sizeof(hdr)))
            return -EFAULT;

        if (hdr.version != cap_version_1 && hdr.version != cap_version_2 &&
            hdr.version != cap_version_3)
        {
            const auto supported = cap_kernel_version;
            if (!lib::copy_to_user(&header->version, &supported, sizeof(supported)))
                return -EFAULT;
            return -EINVAL;
        }

        if (data == nullptr)
            return 0;

        if (hdr.pid < 0)
            return -EINVAL;

        sched::cap_user_data_t kdata { };
        if (const auto ret = sched::capget(hdr.pid, &kdata); !ret)
            return -lib::map_error(ret.error());

        const auto effective = static_cast<std::uint64_t>(kdata.effective);
        const auto permitted = static_cast<std::uint64_t>(kdata.permitted);
        const auto inheritable = static_cast<std::uint64_t>(kdata.inheritable);

        const auto count = cap_data_count(hdr.version);
        std::array<cap_user_data, 2> out { };

        out[0].effective = effective;
        out[0].permitted = permitted;
        out[0].inheritable = inheritable;
        if (count > 1)
        {
            out[1].effective = effective >> 32;
            out[1].permitted = permitted >> 32;
            out[1].inheritable = inheritable >> 32;
        }

        if (!lib::copy_to_user(data, out.data(), sizeof(cap_user_data) * count))
            return -EFAULT;
        return 0;
    }

    int capset(cap_user_header __user *header, const cap_user_data __user *data)
    {
        if (header == nullptr || data == nullptr)
            return -EFAULT;

        cap_user_header hdr { };
        if (!lib::copy_from_user(&hdr, header, sizeof(hdr)))
            return -EFAULT;

        if (hdr.version != cap_version_1 && hdr.version != cap_version_2 &&
            hdr.version != cap_version_3)
        {
            const auto supported = cap_kernel_version;
            if (!lib::copy_to_user(&header->version, &supported, sizeof(supported)))
                return -EFAULT;
            return -EINVAL;
        }

        const auto count = cap_data_count(hdr.version);
        std::array<cap_user_data, 2> in { };
        if (!lib::copy_from_user(in.data(), data, sizeof(cap_user_data) * count))
            return -EFAULT;

        std::uint64_t effective = in[0].effective;
        std::uint64_t permitted = in[0].permitted;
        std::uint64_t inheritable = in[0].inheritable;
        if (count > 1)
        {
            effective |= static_cast<std::uint64_t>(in[1].effective) << 32;
            permitted |= static_cast<std::uint64_t>(in[1].permitted) << 32;
            inheritable |= static_cast<std::uint64_t>(in[1].inheritable) << 32;
        }

        sched::cap_user_data_t kdata {
            .effective = static_cast<sched::cap_t>(effective),
            .permitted = static_cast<sched::cap_t>(permitted),
            .inheritable = static_cast<sched::cap_t>(inheritable)
        };

        if (const auto ret = sched::capset(hdr.pid, &kdata); !ret)
            return -lib::map_error(ret.error());
        return 0;
    }

    int set_tid_address(int __user *tidptr)
    {
        const auto thread = sched::current_thread();
        thread->clear_child_tid = reinterpret_cast<std::uintptr_t>(tidptr);
        return thread->tid;
    }

    unsigned int alarm(unsigned int seconds)
    {
        const auto ns = static_cast<std::uint64_t>(seconds) * 1'000'000'000ul;
        std::uint64_t prev_ns;

        auto proc = sched::current_process();

        if (seconds == 0)
            prev_ns = sched::cancel_alarm(&proc->alarm);
        else
            prev_ns = sched::arm_alarm(&proc->alarm, proc, ns);

        return lib::div_roundup(prev_ns, 1'000'000'000ul);
    }

    namespace
    {
        enum itimer_which : int
        {
            itimer_real = 0,
            itimer_virtual = 1,
            itimer_prof = 2,
        };

        constexpr std::uint64_t timeval_to_ns(const timeval &tv)
        {
            return static_cast<std::uint64_t>(tv.tv_sec) * 1'000'000'000ull +
                static_cast<std::uint64_t>(tv.tv_usec) * 1'000ull;
        }

        constexpr timeval ns_to_timeval(std::uint64_t ns)
        {
            return timeval {
                .tv_sec = static_cast<time_t>(ns / 1'000'000'000ull),
                .tv_usec = static_cast<suseconds_t>((ns % 1'000'000'000ull) / 1'000ull),
            };
        }

        bool valid_timeval(const timeval &tv)
        {
            return tv.tv_sec >= 0 && tv.tv_usec >= 0 && tv.tv_usec < 1'000'000;
        }

        itimerval read_cpu_itimer(sched::cpu_itimer_t &it)
        {
            const std::unique_lock _ { it.lock };
            return itimerval {
                .it_interval = ns_to_timeval(it.interval_ns),
                .it_value = ns_to_timeval(it.value_ns),
            };
        }

        itimerval write_cpu_itimer(
            sched::cpu_itimer_t &it,
            std::uint64_t value_ns, std::uint64_t interval_ns
        )
        {
            const std::unique_lock _ { it.lock };
            const itimerval old {
                .it_interval = ns_to_timeval(it.interval_ns),
                .it_value = ns_to_timeval(it.value_ns),
            };
            it.value_ns = value_ns;
            it.interval_ns = (value_ns == 0) ? 0 : interval_ns;
            return old;
        }
    } // namespace

    int setitimer(int which, const itimerval __user *new_value, itimerval __user *old_value)
    {
        if (which != itimer_real && which != itimer_virtual && which != itimer_prof)
            return -EINVAL;

        itimerval new_v { };
        if (new_value != nullptr)
        {
            if (!lib::copy_from_user(&new_v, new_value, sizeof(new_v)))
                return -EFAULT;
            if (!valid_timeval(new_v.it_value) || !valid_timeval(new_v.it_interval))
                return -EINVAL;
        }

        const auto value_ns = timeval_to_ns(new_v.it_value);
        const auto interval_ns = (value_ns == 0) ? 0 : timeval_to_ns(new_v.it_interval);

        auto proc = sched::current_process();
        itimerval old { };

        switch (which)
        {
            case itimer_real:
            {
                const auto prev = sched::alarm_state(&proc->alarm);
                if (value_ns == 0)
                    sched::cancel_alarm(&proc->alarm);
                else
                    sched::arm_alarm(&proc->alarm, proc, value_ns, interval_ns);

                old.it_value = ns_to_timeval(prev.remaining_ns);
                old.it_interval = ns_to_timeval(prev.interval_ns);
                break;
            }
            case itimer_virtual:
                old = write_cpu_itimer(proc->itimer_virtual, value_ns, interval_ns);
                break;
            case itimer_prof:
                old = write_cpu_itimer(proc->itimer_prof, value_ns, interval_ns);
                break;
        }

        if (old_value != nullptr && !lib::copy_to_user(old_value, &old, sizeof(old)))
            return -EFAULT;

        return 0;
    }

    int getitimer(int which, itimerval __user *curr_value)
    {
        if (which != itimer_real && which != itimer_virtual && which != itimer_prof)
            return -EINVAL;

        if (curr_value == nullptr)
            return -EFAULT;

        auto proc = sched::current_process();
        itimerval cur { };

        switch (which)
        {
            case itimer_real:
            {
                const auto state = sched::alarm_state(&proc->alarm);
                cur.it_value = ns_to_timeval(state.remaining_ns);
                cur.it_interval = ns_to_timeval(state.interval_ns);
                break;
            }
            case itimer_virtual:
                cur = read_cpu_itimer(proc->itimer_virtual);
                break;
            case itimer_prof:
                cur = read_cpu_itimer(proc->itimer_prof);
                break;
        }

        if (!lib::copy_to_user(curr_value, &cur, sizeof(cur)))
            return -EFAULT;

        return 0;
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

        std::shared_ptr<thread_t> target_thread;
        {
            auto locked = target_proc->threads.lock();
            auto it = locked->find(tid);
            if (it != locked->end())
                target_thread = it->second;
        }
        if (!target_thread)
            return -ESRCH;

        if (!check_kill(sig, target_proc.get()))
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
        return send_signal(target_thread.get(), info) ? 0 : -ESRCH;
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

        auto proc = current_process();
        sigaction_t old;
        {
            auto &sigacts = proc->sigactions;
            const std::unique_lock _ { sigacts->lock };
            old = sigacts->actions[signum - 1];
            if (act)
                sigacts->actions[signum - 1] = newact;
        }

        if (act && (newact.handler == sched::sig_ign || (
            newact.handler == sched::sig_dfl &&
            sched::default_for(signum) == sched::default_action::ignore)))
            sched::flush_signal(proc, signum);

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
            if (!lib::copy_from_user(&kset, set, sigsetsize))
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

        if (oldset && !lib::copy_to_user(oldset, &old, sigsetsize))
            return -EFAULT;

        return 0;
    }

    int rt_sigpending(sched::sigset_t __user *set, std::size_t sigsetsize)
    {
        using namespace sched;

        if (sigsetsize != sizeof(sigset_t))
            return -EINVAL;

        auto proc = current_process();
        sigset_t pending;
        {
            const std::unique_lock _ { proc->sigqueue.lock };
            pending = proc->sigqueue.pending;
        }
        pending &= current_thread()->sigmask;

        if (!lib::copy_to_user(set, &pending, sigsetsize))
            return -EFAULT;

        return 0;
    }

    int rt_sigtimedwait(
        const sched::sigset_t __user *uthese, sched::siginfo_t __user *uinfo,
        const timespec __user *uts, std::size_t sigsetsize
    )
    {
        lib::unused(uthese, uinfo, uts, sigsetsize);
        return -ENOSYS;
    }

    int rt_sigsuspend(const sched::sigset_t __user *set, std::size_t sigsetsize)
    {
        using namespace sched;

        if (sigsetsize != sizeof(sigset_t))
            return -EINVAL;

        sigset_t kmask;
        if (!lib::copy_from_user(&kmask, set, sizeof(sigset_t)))
            return -EFAULT;

        scoped_sigmask guard;
        guard.apply(&kmask);

        wait_queue_t queue;
        while (true)
        {
            const auto res = queue.wait();
            if (consume_pending_stops())
                continue;
            if (res.interrupted || res.killed)
                break;
        }

        guard.disarm();
        return -EINTR;
    }

    int sigaltstack(const sched::stack_t __user *ss, sched::stack_t __user *old_ss)
    {
        // TODO
        lib::unused(ss, old_ss);
        return -ENOSYS;
    }

    std::uintptr_t rt_sigreturn()
    {
        return sched::sigreturn();
    }

    int pause()
    {
        sched::wait_queue_t queue;
        while (true)
        {
            queue.wait();
            if (sched::consume_pending_stops())
                continue;
            return -EINTR;
        }
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

    int prlimit(
        pid_t pid, int resource, const sched::rlimit __user *new_limit,
        sched::rlimit __user *old_limit
    )
    {
        using namespace sched;

        if (resource < 0 || resource >= rlimit_nlimits)
            return -EINVAL;

        const auto &caller = current_process()->cred;

        auto target = current_process()->shared_from_this();
        if (pid != 0)
        {
            target = get_process(pid);
            if (!target)
                return -ESRCH;

            const auto &tcred = target->cred;
            if (!tcred)
                return -ESRCH;

            const bool id_match =
                caller->ruid == tcred->ruid &&
                caller->ruid == tcred->euid &&
                caller->ruid == tcred->suid &&
                caller->rgid == tcred->rgid &&
                caller->rgid == tcred->egid &&
                caller->rgid == tcred->sgid;

            if (!id_match && !capable(caller, cap_t::sys_resource))
                return -EPERM;
        }

        if (!target->rlimits)
            return -ESRCH;

        rlimit knew;
        if (new_limit)
        {
            if (!lib::copy_from_user(&knew, new_limit, sizeof(knew)))
                return -EFAULT;
            if (knew.cur > knew.max)
                return -EINVAL;
        }

        const auto kold = target->rlimits->get(resource);
        if (new_limit)
        {
            if (knew.max > kold.max && !capable(caller, cap_t::sys_resource))
                return -EPERM;
            target->rlimits->set(resource, knew);
        }

        if (old_limit && !lib::copy_to_user(old_limit, &kold, sizeof(kold)))
            return -EFAULT;

        return 0;
    }

    int getrlimit(int resource, sched::rlimit __user *rlim)
    {
        if (!rlim)
            return -EFAULT;
        return prlimit(0, resource, nullptr, rlim);
    }

    int setrlimit(int resource, const sched::rlimit __user *rlim)
    {
        if (!rlim)
            return -EFAULT;
        return prlimit(0, resource, rlim, nullptr);
    }

    struct rusage
    {
        timeval ru_utime;
        timeval ru_stime;
        long ru_maxrss;
        long ru_ixrss;
        long ru_idrss;
        long ru_isrss;
        long ru_minflt;
        long ru_majflt;
        long ru_nswap;
        long ru_inblock;
        long ru_oublock;
        long ru_msgsnd;
        long ru_msgrcv;
        long ru_nsignals;
        long ru_nvcsw;
        long ru_nivcsw;
    };

    int getrusage(int who, rusage __user *usage)
    {
        constexpr int rusage_children = -1;
        constexpr int rusage_self = 0;
        constexpr int rusage_thread = 1;

        if (who != rusage_self && who != rusage_children && who != rusage_thread)
            return -EINVAL;

        const rusage kbuf { };
        // TODO
        if (!lib::copy_to_user(usage, &kbuf, sizeof(kbuf)))
            return -EFAULT;
        return 0;
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

    int sched_setaffinity(pid_t pid, std::size_t cpusetsize, const std::uint8_t __user *mask)
    {
        if (pid < 0)
            return -EINVAL;

        if (mask == nullptr)
            return -EFAULT;

        if (cpusetsize == 0 || (cpusetsize & (sizeof(unsigned long) - 1)) != 0)
            return -EINVAL;

        const auto ncpus = cpu::count();
        const auto kernel_bytes = lib::div_roundup(ncpus, 8u);
        const auto read_bytes = std::min(cpusetsize, kernel_bytes);

        lib::bitmap bm { ncpus };
        if (!lib::copy_from_user(bm.data(), mask, read_bytes))
            return -EFAULT;

        const auto trailing = ncpus % 8;
        if (trailing != 0)
            bm.data()[kernel_bytes - 1] &= static_cast<std::uint8_t>((1u << trailing) - 1);

        if (bm.empty())
            return -EINVAL;

        sched::process_t *target_proc = nullptr;
        std::shared_ptr<sched::thread_t> target_thread;
        sched::thread_t *target_thread_ptr = nullptr;
        if (pid == 0)
        {
            target_thread_ptr = sched::current_thread();
            target_proc = sched::current_process();
        }
        else
        {
            target_thread = sched::get_thread(pid);
            if (!target_thread)
                return -ESRCH;
            target_thread_ptr = target_thread.get();
            target_proc = target_thread_ptr->proc;
        }

        const auto cred = sched::current_process()->cred;
        const auto tcred = target_proc->cred;
        const bool perm_ok = sched::capable(cred, sched::cap_t::sys_nice) ||
            (tcred && (cred->euid == tcred->ruid || cred->euid == tcred->suid));

        if (!perm_ok)
            return -EPERM;

        target_thread_ptr->affinity = std::move(bm);
        if (target_thread_ptr->state.load(std::memory_order_acquire) == sched::thread_state::running &&
            !target_thread_ptr->affinity.get(target_thread_ptr->running_on->idx))
            target_thread_ptr->set_flag(sched::thread_flags::needs_resched);

        return 0;
    }

    int sched_getaffinity(pid_t pid, std::size_t cpusetsize, std::uint8_t __user *mask)
    {
        if (pid < 0)
            return -EINVAL;

        if (mask == nullptr)
            return -EFAULT;

        if (cpusetsize == 0 || (cpusetsize & (sizeof(unsigned long) - 1)) != 0)
            return -EINVAL;

        const auto ncpus = cpu::count();
        const auto kernel_bytes = lib::div_roundup(ncpus, 8u);
        constexpr auto ulong_size = sizeof(unsigned long);
        const auto kernel_bytes_aligned = (kernel_bytes + ulong_size - 1) & ~(ulong_size - 1);

        if (cpusetsize < kernel_bytes_aligned)
            return -EINVAL;

        std::shared_ptr<sched::thread_t> target_thread;
        sched::thread_t *target_thread_ptr = nullptr;
        if (pid != 0)
        {
            target_thread = sched::get_thread(pid);
            if (!target_thread)
                return -ESRCH;
            target_thread_ptr = target_thread.get();
        }
        else target_thread_ptr = sched::current_thread();

        const auto &bm = target_thread_ptr->affinity;
        const auto first = std::min(kernel_bytes, cpusetsize);
        if (!lib::copy_to_user(mask, bm.data(), first))
            return -EFAULT;
        if (cpusetsize > first && !lib::fill_user(mask + first, 0, cpusetsize - first))
            return -EFAULT;

        return static_cast<int>(kernel_bytes_aligned);
    }

    [[noreturn]] void exit_group(int status)
    {
        sched::process_exit(status);
        std::unreachable();
    }

    [[noreturn]] void exit(int status)
    {
        sched::thread_exit(status);
        std::unreachable();
    }
} // namespace syscall::proc
