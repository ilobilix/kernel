// Copyright (C) 2024-2026  ilobilo

export module system.sched:signal;

import system.cpu.regs;
import lib;
import std;

export namespace sched
{
    constexpr std::size_t nsig = 64;

    enum signo : int
    {
        sighup    = 1,
        sigint    = 2,
        sigquit   = 3,
        sigill    = 4,
        sigtrap   = 5,
        sigabrt   = 6,
        sigbus    = 7,
        sigfpe    = 8,
        sigkill   = 9,
        sigusr1   = 10,
        sigsegv   = 11,
        sigusr2   = 12,
        sigpipe   = 13,
        sigalrm   = 14,
        sigterm   = 15,
        sigstkflt = 16,
        sigchld   = 17,
        sigcont   = 18,
        sigstop   = 19,
        sigtstp   = 20,
        sigttin   = 21,
        sigttou   = 22,
        sigurg    = 23,
        sigxcpu   = 24,
        sigxfsz   = 25,
        sigvtalrm = 26,
        sigprof   = 27,
        sigwinch  = 28,
        sigio     = 29,
        sigpwr    = 30,
        sigsys    = 31,
        sigrtmin  = 32,
        sigrtmax  = 64
    };

    constexpr std::size_t sigset_words = lib::div_roundup(nsig, 64);
    struct sigset_t
    {
        std::uint64_t bits[sigset_words] { };

        constexpr void add(int sig)
        {
            bits[(sig - 1) / 64] |= (1ul << ((sig - 1) % 64));
        }

        constexpr void rem(int sig)
        {
            bits[(sig - 1) / 64] &= ~(1ul << ((sig - 1) % 64));
        }

        constexpr bool has(int sig) const
        {
            return bits[(sig - 1) / 64] & (1ul << ((sig - 1) % 64));
        }

        constexpr int lowest() const
        {
            for (std::size_t i = 1; i < nsig  +1; i++)
            {
                if (has(i))
                    return i;
            }
            return 0;
        }

        constexpr void fill()
        {
            for (auto &word : bits)
                word = ~0ul;
        }

        constexpr void clear()
        {
            for (auto &word : bits)
                word = 0;
        }

        constexpr bool any() const
        {
            for (const auto &word : bits)
            {
                if (word)
                    return true;
            }
            return false;
        }

        constexpr sigset_t operator|(const sigset_t &rhs) const
        {
            sigset_t out { };
            for (std::size_t i = 0; i < sigset_words; i++)
                out.bits[i] = bits[i] | rhs.bits[i];
            return out;
        }

        constexpr sigset_t &operator|=(const sigset_t &rhs)
        {
            for (std::size_t i = 0; i < sigset_words; i++)
                bits[i] |= rhs.bits[i];
            return *this;
        }

        constexpr sigset_t operator&(const sigset_t &rhs) const
        {
            sigset_t out { };
            for (std::size_t i = 0; i < sigset_words; i++)
                out.bits[i] = bits[i] & rhs.bits[i];
            return out;
        }

        constexpr sigset_t &operator&=(const sigset_t &rhs)
        {
            for (std::size_t i = 0; i < sigset_words; i++)
                bits[i] &= rhs.bits[i];
            return *this;
        }

        constexpr sigset_t operator~() const
        {
            sigset_t out { };
            for (std::size_t i = 0; i < sigset_words; i++)
                out.bits[i] = ~bits[i];
            return out;
        }
    };

    constexpr sigset_t sigmask_uncatchable = [] {
        sigset_t ret { };
        ret.add(sigkill);
        ret.add(sigstop);
        return ret;
    } ();

    enum sigprocmask_how : int
    {
        sig_block = 0,
        sig_unblock = 1,
        sig_setmask = 2
    };

    enum si_code : int
    {
        si_user = 0,
        si_kernel = 0x80,
        // si_queue = -1,
        si_tkill = -6,
        cld_exited = 1,
        cld_killed = 2,
        cld_dumped = 3,
        // cld_trapped = 4,
        cld_stopped = 5,
        cld_continued = 6
    };

    struct siginfo_t
    {
        int signo;
        int code;
        int err;
        pid_t pid;
        uid_t uid;
        int status;
        std::uintptr_t addr;
        std::uintptr_t value;
    };

    struct stack_t
    {
        std::uintptr_t sp = 0;
        int flags = 0;
        std::size_t size = 0;
    };

    using sighandler_t = std::uintptr_t;

    constexpr sighandler_t sig_dfl = 0;
    constexpr sighandler_t sig_ign = 1;

    enum sa_flags : unsigned long
    {
        sa_nocldstop = 0x00000001,
        sa_nocldwait = 0x00000002,
        sa_siginfo   = 0x00000004,
        sa_restorer  = 0x04000000,
        sa_onstack   = 0x08000000,
        sa_restart   = 0x10000000,
        sa_nodefer   = 0x40000000,
        sa_resethand = 0x80000000
    };

    struct sigaction_t
    {
        sighandler_t handler = sig_dfl;
        unsigned long flags = 0;
        sighandler_t restorer = 0;
        sigset_t mask { };
    };

    struct signal_actions_t
    {
        sigaction_t actions[nsig] { };
        lib::spinlock lock;

        std::shared_ptr<signal_actions_t> clone() const
        {
            auto cloned = std::make_shared<signal_actions_t>();
            std::memcpy(cloned->actions, actions, sizeof(actions));
            return cloned;
        }
    };

    struct pending_signal_t
    {
        siginfo_t info;
    };

    struct signal_queue_t
    {
        sigset_t pending { };
        lib::list<pending_signal_t> queue;
        lib::spinlock lock;
    };

    enum class default_action
    {
        term,
        ignore,
        core,
        stop,
        cont
    };

    constexpr default_action default_for(int sig)
    {
        switch (sig)
        {
            case sigchld:
            case sigurg:
            case sigwinch:
                return default_action::ignore;
            case sigcont:
                return default_action::cont;
            case sigstop:
            case sigtstp:
            case sigttin:
            case sigttou:
                return default_action::stop;
            case sigquit:
            case sigill:
            case sigtrap:
            case sigabrt:
            case sigbus:
            case sigfpe:
            case sigsegv:
            case sigxcpu:
            case sigxfsz:
            case sigsys:
                return default_action::core;
            default:
                return default_action::term;
        }
    }

    struct process_t;
    struct thread_t;

    bool send_signal(thread_t *thread, const siginfo_t &info);
    bool send_signal(process_t *process, const siginfo_t &info);

    void flush_signal(process_t *proc, int sig);

    void handle_pending_signals(cpu::registers *regs);
    std::uintptr_t sigreturn();
} // export namespace sched
