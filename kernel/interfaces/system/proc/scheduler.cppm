// Copyright (C) 2024-2026  ilobilo

export module system.sched;

export import system.sched.thread_base;
export import system.sched.wait_queue;
export import system.sched.mutex;
export import system.sched.cred;

export import :perm;
export import :nice;
export import :signal;
export import :process;
export import :thread;
export import :sleep;
export import :run_queue;
export import :work_queue;

import :arch;
import boot;

// implemented in :arch
namespace sched::arch
{
    struct context;
    struct data;

    // makes sure that thread doesn't migrate to another core
    thread_t *current_thread();

    void init_core(thread_t *initial);
    void init_thread(
        thread_t *thread, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone
    );
    void deinit_thread(thread_t *thread);

    void arm_timer_ns(std::uint64_t ns);
    void wake_up_other(std::size_t cpu_idx);

    void context_switch(thread_t *prev, thread_t *next);
    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack);
} // namespace sched::arch

namespace sched
{
    bool _running = false;
} // namespace sched

export namespace sched
{
    constexpr std::size_t balance_interval_ns = 100'000'000;
    constexpr std::size_t balance_max_nr = 4;

    constexpr std::size_t kstack_size = boot::kstack_size;
    constexpr std::size_t ustack_size = boot::ustack_size;

    lib::initgraph::stage *pid0_created_stage();

    // start scheduling on this code
    [[noreturn]] void start();

    [[noreturn]] inline void jump_to_user(std::uintptr_t ip, std::uintptr_t stack)
    {
        arch::return_to_user(ip, stack);
    }

    inline thread_t *current_thread()
    {
        return arch::current_thread();
    }

    inline process_t *current_process()
    {
        return current_thread()->proc;
    }

    inline void preempt_disable()
    {
        if (!_running) [[unlikely]]
            return;
        current_thread()->preempt_count.fetch_add(1, std::memory_order_acquire);
    }

    inline bool preempt_enable()
    {
        if (!_running) [[unlikely]]
            return false;
        return current_thread()->preempt_count.fetch_sub(1, std::memory_order_release) == 1;
    }

    inline bool is_preempt_disabled()
    {
        return _running && current_thread()->preempt_count.load(std::memory_order_relaxed) > 0;
    }

    inline bool is_running()
    {
        return _running;
    }

    // pick next thread and switch to it
    // called on yield, block, timer or wake up
    void schedule();

    process_t *create_process(process_t *parent);

    // create a new kernel thread under pid 0
    thread_t *create_kthread(std::uintptr_t ip, std::uintptr_t arg, nice_t nice = default_nice);

    // create a user thread
    thread_t *create_uthread(
        process_t *proc, std::uintptr_t ip, std::uintptr_t arg,
        bool is_trampoline, bool is_clone,
        std::uintptr_t stack, nice_t nice = default_nice
    );

    // enqueue a new thread on current cpu
    void enqueue_new(thread_t *thread);

    // create a new kernel thread and enqueue it
    thread_t *spawn(std::uintptr_t ip, std::uintptr_t arg = 0, nice_t nice = default_nice);

    template<typename Func>
    inline thread_t *spawn(Func &&func, std::uintptr_t arg = 0, nice_t nice = default_nice)
    {
        return spawn(reinterpret_cast<std::uintptr_t>(func), arg, nice);
    }

    template<typename Func, typename Arg>
    inline thread_t *spawn(Func &&func, Arg arg, nice_t nice = default_nice)
    {
        return spawn(
            reinterpret_cast<std::uintptr_t>(func),
            reinterpret_cast<std::uintptr_t>(arg), nice
        );
    }

    bool wake_up(thread_t *thread, bool preempt = true);

    bool yield();

    // exit the current thread
    // if this is the last thread, process becomes a zombie
    [[noreturn]] void thread_exit(int exit_code);

    // exit the current process and kill all threads
    [[noreturn]] void process_exit(int exit_code);

    // exit the current process as if killed by signal signo
    // status reported to waitpid will have WIFSIGNALED set
    [[noreturn]] void process_exit_signal(int signo, bool core_dumped = false);

    // get process with the pid
    process_t *get_process(pid_t pid);

    // called from a timer interrupt
    void tick();

    void load_balance();

    void set_affinity(pid_t pid, lib::bitmap mask);
    lib::bitmap get_affinity(pid_t pid);

    int setpgid(pid_t pid, pid_t pgid);
    pid_t setsid();

    enum clone_flags : std::uint64_t
    {
        csignal              = 0x000000FF,    // signal mask to be sent at exit
        clone_vm             = 0x00000100,    // set if VM shared between processes
        clone_fs             = 0x00000200,    // set if fs info shared between processes
        clone_files          = 0x00000400,    // set if open files shared between processes
        clone_sighand        = 0x00000800,    // set if signal handlers and blocked signals shared
        clone_pidfd          = 0x00001000,    // set if a pidfd should be placed in parent
        clone_ptrace         = 0x00002000,    // set if we want to let tracing continue on the child too
        clone_vfork          = 0x00004000,    // set if the parent wants the child to wake it up on mm_release
        clone_parent         = 0x00008000,    // set if we want to have the same parent as the cloner
        clone_thread         = 0x00010000,    // same process
        clone_newns          = 0x00020000,    // new mount namespace group
        clone_sysvsem        = 0x00040000,    // share system V SEM_UNDO semantics
        clone_settls         = 0x00080000,    // create a new TLS for the child
        clone_parent_settid  = 0x00100000,    // set the TID in the parent
        clone_child_cleartid = 0x00200000,    // clear the TID in the child
        clone_detached       = 0x00400000,    // unused, ignored
        clone_untraced       = 0x00800000,    // set if the tracing process can't force CLONE_PTRACE on this clone
        clone_child_settid   = 0x01000000,    // set the TID in the child
        clone_newcgroup      = 0x02000000,    // new cgroup namespace
        clone_newuts         = 0x04000000,    // new utsname namespace
        clone_newipc         = 0x08000000,    // new ipc namespace
        clone_newuser        = 0x10000000,    // new user namespace
        clone_newpid         = 0x20000000,    // new pid namespace
        clone_newnet         = 0x40000000,    // new network namespace
        clone_io             = 0x80000000,    // clone io context
        clone_clear_sighand  = 0x100000000ul, // clear any signal handler and reset to SIG_DFL.
        clone_into_cgroup    = 0x200000000ul, // clone into a specific cgroup given the right permissions.
        clone_newtime        = 0x00000080,    // new time namespace
    };

    struct kclone_args_t
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

    pid_t clone(const kclone_args_t &args);
    int exec(
        const vfs::path &path, std::vector<std::string> argv,
        std::vector<std::string> envp, std::string pathname
    );

    enum wait_flags
    {
        wnohang = 0x00000001,
        wuntraced = 0x00000002,
        wstopped = wuntraced,
        wexited = 0x00000004,
        wcontinued = 0x00000008,
        wnowait = 0x01000000
    };

    pid_t waitpid(pid_t wait_pid, int options, int *status);

    int kill(pid_t pid, int sig);
} // export namespace sched
