// Copyright (C) 2024-2025  ilobilo

module x86_64.system.syscall;

import system.scheduler;
import system.cpu;
import lib;
import cppstd;

namespace x86_64::syscall::arch
{
    int arch_prctl(int op, unsigned long __user *addr)
    {
        const auto thread = sched::this_thread();
        const auto address = reinterpret_cast<std::uintptr_t>(addr);
        const auto space = lib::classify_address(address, sizeof(unsigned long));
        if (space != lib::address_space::user)
            return (errno = EFAULT, -1);
        switch (op)
        {
            case 0x1001: // ARCH_SET_GS
                cpu::gs::write_kernel(thread->gs_base = address);
                break;
            case 0x1002: // ARCH_SET_FS
                cpu::fs::write(thread->fs_base = address);
                break;
            case 0x1003: // ARCH_GET_FS
                if (!lib::copy_to_user(addr, &thread->fs_base, sizeof(unsigned long)))
                    return (errno = EFAULT, -1);
                break;
            case 0x1004: // ARCH_GET_GS
                if (!lib::copy_to_user(addr, &thread->gs_base, sizeof(unsigned long)))
                    return (errno = EFAULT, -1);
                break;
            default:
                return (errno = EINVAL, -1);
        }
        return 0;
    }
} // namespace x86_64::syscall::arch