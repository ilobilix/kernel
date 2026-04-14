// Copyright (C) 2024-2025  ilobilo

module;

#include <user.h>

export module x86_64.system.syscall:arch;

export namespace x86_64::syscall::arch
{
    int arch_prctl(int op, unsigned long __user *addr);
} // namespace export x86_64::syscall::arch