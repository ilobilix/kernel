// Copyright (C) 2024-2025  ilobilo

export module system.syscall.time;
import lib;

export namespace syscall::time
{
    int clock_gettime(clockid_t clockid, timespec __user *tp);
} // export namespace syscall::time