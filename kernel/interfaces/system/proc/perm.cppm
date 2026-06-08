// Copyright (C) 2024-2026  ilobilo

export module system.sched:perm;

import system.sched.cred;
import system.vfs.caps;
import magic_enum;
import lib;
import std;

import :thread;
import :process;

export namespace sched
{
    bool capable(const std::shared_ptr<cred_t> &cred, cap_t cap);
    bool capable(cap_t cap);

    enum class access_mode
    {
        none = 0,
        read = (1 << 0),
        write = (1 << 1),
        exec = (1 << 2) // search
    };

    using namespace magic_enum::bitwise_operators;

    bool check_perms(const std::shared_ptr<cred_t> &cred, const stat &stat, access_mode desired);
    bool check_perms(const stat &stat, access_mode desired);

    // check if current process can send a signal to target
    bool check_kill(int sig, const process_t *target);

    lib::expect<void> setuid(uid_t uid);
    lib::expect<void> setreuid(uid_t ruid, uid_t euid);
    lib::expect<void> setresuid(uid_t ruid, uid_t euid, uid_t suid);

    lib::expect<void> setgid(gid_t gid);
    lib::expect<void> setregid(gid_t rgid, gid_t egid);
    lib::expect<void> setresgid(gid_t rgid, gid_t egid, gid_t sgid);

    uid_t setfsuid(uid_t fsuid);
    gid_t setfsgid(gid_t fsgid);

    lib::expect<void> setgroups(lib::maybe_uspan<gid_t> groups);
    lib::expect<std::size_t> getgroups(lib::maybe_uspan<gid_t> groups);

    // not the same size as the one used for syscalls
    struct cap_user_data_t
    {
        cap_t effective;
        cap_t permitted;
        cap_t inheritable;
    };

    lib::expect<void> capget(pid_t pid, cap_user_data_t *data);
    lib::expect<void> capset(pid_t pid, const cap_user_data_t *data);

    lib::expect<void> cap_bounding_drop(cap_t cap);
    lib::expect<void> cap_ambient_raise(cap_t cap);
    void cap_ambient_lower(cap_t cap);

    lib::expect<void> set_securebits(secbit_t securebits);

    void apply_exec_caps(process_t *process, const stat &stat, std::optional<vfs::file_caps> fcaps);
} // export namespace sched
