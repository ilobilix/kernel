// Copyright (C) 2024-2026  ilobilo

export module system.sched;

export import system.sched.thread_base;

export import :cred;
export import :perm;
export import :nice;
export import :process;
export import :thread;
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
    void init_thread(thread_t *thread, std::uintptr_t ip, std::uintptr_t arg, bool is_trampoline);

    void arm_timer_ns(std::uint64_t ns);
    void wake_up_other(std::size_t cpu_idx);

    void context_switch(thread_t *prev, thread_t *next);
    [[noreturn]] void return_to_user(std::uintptr_t ip, std::uintptr_t stack);
} // namespace sched::arch

namespace sched
{
    bool running = false;
} // namespace sched

export namespace sched
{
    constexpr std::size_t balance_interval_ns = 100'000'000;
    constexpr std::size_t balance_max_nr = 4;

    constexpr std::size_t kstack_size = boot::kstack_size;
    constexpr std::size_t ustack_size = boot::ustack_size;

    struct sleep_entry_t
    {
        thread_t *thread;
        std::uint64_t deadline_ns;
        bool expired;

        lib::rbtree_hook<sleep_entry_t> hook;
    };

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
        if (!running) [[unlikely]]
            return;
        current_thread()->preempt_count.fetch_add(1, std::memory_order_acquire);
    }

    inline bool preempt_enable()
    {
        if (!running) [[unlikely]]
            return false;
        return current_thread()->preempt_count.fetch_sub(1, std::memory_order_release) == 1;
    }

    inline bool is_preempt_disabled()
    {
        return running && current_thread()->preempt_count.load(std::memory_order_relaxed) > 0;
    }

    inline bool is_running()
    {
        return running;
    }

    // pick next thread and switch to it
    // called on yield, block, timer or wake up
    void schedule();

    // create a new kernel thread under pid 0
    thread_t *create_kthread(std::uintptr_t ip, std::uintptr_t arg, nice_t nice = default_nice);

    // create a user thread
    thread_t *create_uthread(
        process_t *proc, std::uintptr_t ip, std::uintptr_t arg, bool is_trampoline,
        std::uintptr_t stack, nice_t nice = default_nice
    );

    // enqueue a new thread on current cpu
    void enqueue_new(thread_t *thread);

    // create a new kernel thread and enqueue it
    thread_t *spawn(std::uintptr_t ip, std::uintptr_t arg = 0, nice_t nice = default_nice);

    template<std::invocable Func>
    inline thread_t *spawn(Func &&func, std::uintptr_t arg = 0, nice_t nice = default_nice)
    {
        return spawn(reinterpret_cast<std::uintptr_t>(func), arg, nice);
    }

    bool wake_up(thread_t *thread, bool preempt = true);

    // put the current thread to sleep
    void sleep();

    // put the current thread to uninterruptible sleep
    void block();

    // puts entry in sleep list
    void arm_thread_timeout(sleep_entry_t *entry, std::uint64_t ns);

    // if expired, returns false and removes entry
    bool cancel_thread_timeout(sleep_entry_t *entry);

    // sleep for nanoseconds. return remaining time if interrupted
    std::uint64_t sleep_for_ns(std::uint64_t ns);

    bool yield();

    // exit the current thread
    // if this is the last thread, process becomes a zombie
    [[noreturn]] void thread_exit(int exit_code);

    // exit the current process and kill all threads
    [[noreturn]] void process_exit(int exit_code);

    // get process with the pid
    process_t *get_process(pid_t pid);

    // called from a timer interrupt
    void tick();

    void load_balance();

    void set_affinity(pid_t pid, lib::bitmap mask);
    lib::bitmap get_affinity(pid_t pid);

    int setpgid(pid_t pid, pid_t pgid);
    pid_t getpgid(pid_t pid);

    pid_t setsid();
    pid_t getsid(pid_t pid);

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

    pid_t clone(const kclone_args &args);
    int exec(const vfs::path &path, std::vector<std::string> argv, std::vector<std::string> envp);

    struct wait_options_t
    {
        enum {
            wnohang = 0x00000001,
            wuntraced = 0x00000002,
            wstopped = wuntraced,
            wexited = 0x00000004,
            wcontinued = 0x00000008,
            wnowait = 0x01000000
        };

        // -1 = any child
        // >0 = specific pid
        // 0 = same pgroupd
        // <-1 = specific pgroup
        pid_t pid;
        int options;
    };

    pid_t waitpid(const wait_options_t &options, int *status);
} // export namespace sched
