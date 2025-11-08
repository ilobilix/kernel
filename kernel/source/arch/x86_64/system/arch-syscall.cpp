// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>
#include <user.h>

module x86_64.system.syscall;

import system.scheduler;
import system.cpu;
import lib;
import cppstd;

namespace x86_64::syscall::arch
{
    int arch_prctl(int op, unsigned long __user *addr)
    {
        auto thread = sched::this_thread();
        auto address = reinterpret_cast<std::uintptr_t>(addr);
        switch (op)
        {
            case 0x1001: // ARCH_SET_GS
                cpu::gs::write_kernel(thread->gs_base = address);
                break;
            case 0x1002: // ARCH_SET_FS
                cpu::fs::write(thread->fs_base = address);
                break;
            case 0x1003: // ARCH_GET_FS
                lib::copy_to_user(addr, &thread->fs_base, sizeof(unsigned long));
                break;
            case 0x1004: // ARCH_GET_GS
                lib::copy_to_user(addr, &thread->gs_base, sizeof(unsigned long));
                break;
            default:
                return -EINVAL;
        }
        return 0;
    }
} // namespace x86_64::syscall::arch