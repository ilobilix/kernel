// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.syscall;

export namespace x86_64::syscall
{
    bool is_in_syscall();
    void init_cpu();
} // export namespace x86_64::syscall