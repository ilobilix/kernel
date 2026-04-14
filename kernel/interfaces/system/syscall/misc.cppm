// Copyright (C) 2024-2025  ilobilo

export module system.syscall.misc;
import lib;

export namespace syscall::misc
{
    int uname(struct utsname __user *buf);
} // export namespace syscall::misc