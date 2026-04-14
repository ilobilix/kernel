// Copyright (C) 2024-2025  ilobilo

export module system.syscall.misc;

import lib;
import cppstd;

export namespace syscall::misc
{
    int uname(struct utsname __user *buf);
    std::ssize_t getrandom(void __user *buf, std::size_t buflen, unsigned int flags);
} // export namespace syscall::misc