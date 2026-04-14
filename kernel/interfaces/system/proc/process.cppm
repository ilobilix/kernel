// Copyright (C) 2024-2026  ilobilo

export module system.sched:process;

import system.sched.wait_queue;
import system.sched.mutex;
import system.sched.cred;
import system.memory.virt;
import system.vfs;
import lib;
import std;

import :thread;

export namespace sched
{
    constexpr std::size_t nsig = 64;

    // TODO
    struct signal_action_t;
    struct signal_actions_t
    {
        // signal_action_t actions[64];
        lib::spinlock lock;

        std::shared_ptr<signal_actions_t> clone() const
        {
            auto cloned = std::make_shared<signal_actions_t>();
            // TODO
            return cloned;
        }
    };

    struct signal_queue_t
    {
        lib::spinlock lock;
    };

    struct group_t;
    struct session_t;

    struct process_t
    {
        pid_t pid;

        process_t *parent;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, lib::spinlock
        > children;

        std::shared_ptr<group_t> group;
        std::shared_ptr<session_t> session;

        std::shared_ptr<vmm::vmspace> vmspace;

        struct vfs_state
        {
            vfs::path root;
            vfs::path cwd;
            mode_t umask = static_cast<mode_t>(s_iwgrp | s_iwoth);

            std::shared_ptr<vfs_state> clone() const
            {
                auto cloned = std::make_shared<vfs_state>(*this);
                cloned->root = root;
                cloned->cwd = cwd;
                cloned->umask = umask;
                return cloned;
            }
        };
        std::shared_ptr<vfs_state> vfs;
        std::shared_ptr<vfs::fdtable> fdt;

        std::shared_ptr<cred_t> cred;

        std::shared_ptr<signal_actions_t> sigactions;
        signal_queue_t sigqueue;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                thread_t *
            >, lib::spinlock
        > threads;

        std::atomic<std::size_t> alive_threads = 0;

        int exit_code = 0;
        bool is_zombie = false;
        bool has_execved = false;

        wait_queue_t wait_child;

        recursive_mutex lock;

        ~process_t();
    };

    struct group_t
    {
        pid_t pgid;
        std::shared_ptr<session_t> session;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                process_t *
            >, lib::spinlock
        > members;

        int signal_all(int sig);
    };

    struct session_t
    {
        pid_t sid;

        lib::locker<
            lib::map::flat_hash<
                pid_t,
                std::weak_ptr<group_t>
            >, lib::spinlock
        > members;

        lib::locker<
            std::shared_ptr<
                void
            >, lib::spinlock
        > ctty;

        std::weak_ptr<group_t> foreground_pg;
    };
} // export namespace sched
