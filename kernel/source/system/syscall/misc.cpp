// Copyright (C) 2024-2025  ilobilo

module;

#include <uacpi/sleep.h>

module system.syscall.misc;

import system.scheduler;
import lib;
import std;

namespace syscall::misc
{
    struct utsname
    {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    };

    int uname(struct utsname __user *buf)
    {
        utsname kbuf
        {
            .sysname = "Ilobilix",
            .nodename = "ilobilix",
            .release = "0.0.1",
            .version = __DATE__ " " __TIME__,
            .machine = "x86_64",
            .domainname = "(none)"
        };
        if (!lib::copy_to_user(buf, &kbuf, sizeof(utsname)))
            return (errno = EFAULT, -1);
        return 0;
    }

    int reboot(int magic, int magic2, int op, void __user *arg)
    {
        lib::unused(arg);

        const auto umagic = static_cast<std::uint32_t>(magic);
        const auto umagic2 = static_cast<std::uint32_t>(magic2);
        if (umagic != 0xFEE1DEAD || (umagic2 != 0x28121969 && umagic2 != 0x05121996 &&
            umagic2 != 0x16041998 && umagic2 != 0x20112000))
            return (errno = EINVAL, -1);

        // TODO: only root can call reboot
        // TODO: actual reboot and shutdown
        switch (static_cast<std::uint32_t>(op))
        {
            case 0xCDEF0123: // LINUX_REBOOT_CMD_HALT
                lib::panic("system halted");
                break;
            case 0x89ABCDEF: // LINUX_REBOOT_CMD_CAD_ON
                break;
            case 0x00000000: // LINUX_REBOOT_CMD_CAD_OFF
                break;
            case 0x4321FEDC: // LINUX_REBOOT_CMD_POWER_OFF
                uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
                uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
                lib::panic("power off failed");
                break;
            case 0x01234567: // LINUX_REBOOT_CMD_RESTART
            case 0xA1B2C3D4: // LINUX_REBOOT_CMD_RESTART2
                uacpi_reboot();
                lib::panic("reboot failed");
                break;
            case 0xD000FCE2: // LINUX_REBOOT_CMD_SW_SUSPEND
                lib::panic("suspend not implemented");
                break;
            case 0x45584543: // LINUX_REBOOT_CMD_KEXEC
                lib::panic("kexec not implemented");
                break;
            default:
                return (errno = EINVAL, -1);
        }
        return 0;
    }

    std::ssize_t getrandom(void __user *buf, std::size_t buflen, unsigned int flags)
    {
        lib::unused(flags);
        if (buflen == 0)
            return 0;

        auto uspan = lib::maybe_uspan<std::byte>::create(lib::remove_user_cast<std::byte>(buf), buflen);
        if (!uspan)
            return (errno = EFAULT, -1);
        return lib::random_bytes(uspan.value());
    }
} // namespace syscall::misc