// Copyright (C) 2024-2026  ilobilo

export module system.sched:process;

import drivers.fs.dev.tty;
import system.memory.virt;
import system.vfs;
import lib;
import std;

import :cred;
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

        group_t *group;
        session_t *session;

        std::shared_ptr<vmm::vmspace> vmspace;

        struct vfs_state
        {
            vfs::path root;
            vfs::path cwd;
            mode_t umask = static_cast<mode_t>(s_iwgrp | s_iwoth);
        };
        std::shared_ptr<vfs_state> vfs;
        std::shared_ptr<vfs::fdtable> fds;

        std::shared_ptr<cred_t> cred;

        std::shared_ptr<signal_action_t> sigactions;
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

        lib::spinlock lock;
    };

    struct group_t
    {
        pid_t pgid;
        session_t *session;
        process_t *leader;

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
        process_t *leader;

        lib::locker<
            lib::map::flat_hash<
                pid_t, group_t *
            >, lib::spinlock
        > members;

        lib::locker<
            std::shared_ptr<
                fs::dev::tty::instance
            >, lib::spinlock
        > ctty;

        group_t *foreground_pg;
    };
} // export namespace sched
