// Copyright (C) 2024-2025  ilobilo

export module system.syscall.misc;

import lib;
import std;

export namespace syscall::misc
{
    int uname(struct utsname __user *buf);
    int reboot(int magic, int magic2, int op, void __user *arg);
    std::ssize_t getrandom(void __user *buf, std::size_t buflen, unsigned int flags);
} // export namespace syscall::misc