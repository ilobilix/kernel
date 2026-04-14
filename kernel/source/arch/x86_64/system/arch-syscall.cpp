// Copyright (C) 2024-2026  ilobilo

module x86_64.system.syscall;

import system.sched;
import system.cpu;
import lib;
import std;

namespace x86_64::syscall::arch
{
    int arch_prctl(int op, unsigned long __user *addr)
    {
        const auto thread = sched::current_thread();
        const auto address = reinterpret_cast<std::uintptr_t>(addr);
        const auto space = lib::classify_address(address, sizeof(unsigned long));
        if (space != lib::address_space::user)
            return (errno = EFAULT, -1);
        switch (op)
        {
            case 0x1001: // ARCH_SET_GS
                cpu::gs::write_kernel(thread->adata.gs_base = address);
                break;
            case 0x1002: // ARCH_SET_FS
                cpu::fs::write(thread->adata.fs_base = address);
                break;
            case 0x1003: // ARCH_GET_FS
                if (!lib::copy_to_user(addr, &thread->adata.fs_base, sizeof(unsigned long)))
                    return (errno = EFAULT, -1);
                break;
            case 0x1004: // ARCH_GET_GS
                if (!lib::copy_to_user(addr, &thread->adata.gs_base, sizeof(unsigned long)))
                    return (errno = EFAULT, -1);
                break;
            default:
                lib::error("arch_prctl: unhandled operation: 0x{:X}", op);
                return (errno = EINVAL, -1);
        }
        return 0;
    }
} // namespace x86_64::syscall::arch
