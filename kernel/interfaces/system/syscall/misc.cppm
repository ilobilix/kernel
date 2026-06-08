// Copyright (C) 2024-2026  ilobilo

export module system.syscall.misc;

import lib;
import std;

export namespace syscall::misc
{
    int uname(struct utsname __user *buf);
    int sethostname(const char __user *name, std::size_t len);
    int reboot(int magic, int magic2, int op, void __user *arg);

    int vhangup();

    int sysinfo(struct sysinfo __user *info);

    std::ssize_t getrandom(void __user *buf, std::size_t buflen, unsigned int flags);

    int prctl(
        int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5
    );

    int seccomp(unsigned int op, unsigned int flags, void __user *args);

    int syslog(int type, char __user *buf, int len);

    int landlock_create_ruleset(
        const struct landlock_ruleset_attr __user *const attr, const std::size_t size,
        const std::uint32_t flags
    );
} // export namespace syscall::misc
