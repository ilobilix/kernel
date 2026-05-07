// Copyright (C) 2024-2026  ilobilo

module;

#include <uacpi/sleep.h>
#include <version.h>

module system.syscall.misc;

import system.memory.phys;
import system.random;
import system.chrono;
import system.sched;
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

    struct sysinfo
    {
        long uptime;
        unsigned long loads[3];
        unsigned long totalram;
        unsigned long freeram;
        unsigned long sharedram;
        unsigned long bufferram;
        unsigned long totalswap;
        unsigned long freeswap;
        unsigned short procs;
        unsigned short pad;
        unsigned long totalhigh;
        unsigned long freehigh;
        unsigned int mem_unit;
        char _f[20 - 2 * sizeof(long) - sizeof(int)];
    };

    int uname(struct utsname __user *buf)
    {
        utsname kbuf
        {
            .sysname = "Ilobilix",
            .nodename = "ilobilix",
            .release = ILOBILIX_RELEASE,
            .version = __DATE__ " " __TIME__,
            .machine = ILOBILIX_ARCH,
            .domainname = "(none)"
        };
        if (!lib::copy_to_user(buf, &kbuf, sizeof(utsname)))
            return -EFAULT;
        return 0;
    }

    int reboot(int magic, int magic2, int op, void __user *arg)
    {
        lib::unused(arg);

        const auto umagic = static_cast<std::uint32_t>(magic);
        const auto umagic2 = static_cast<std::uint32_t>(magic2);
        if (umagic != 0xFEE1DEAD || (umagic2 != 0x28121969 && umagic2 != 0x05121996 &&
            umagic2 != 0x16041998 && umagic2 != 0x20112000))
            return -EINVAL;

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
                return -EINVAL;
        }
        return 0;
    }

    int sysinfo(struct sysinfo __user *info)
    {
        const auto mem = pmm::info();
        const auto uptime_ns = chrono::now(chrono::monotonic);

        const auto procs = sched::process_count();

        struct sysinfo kbuf { };
        // TODO
        kbuf.uptime = uptime_ns.tv_sec;
        kbuf.totalram = mem.usable;
        kbuf.freeram = mem.usable - mem.used;
        kbuf.procs = static_cast<unsigned short>(
            std::min<std::size_t>(procs, std::numeric_limits<unsigned short>::max())
        );
        kbuf.mem_unit = 1;

        if (!lib::copy_to_user(info, &kbuf, sizeof(kbuf)))
            return -EFAULT;
        return 0;
    }

    int vhangup()
    {
        const auto proc = sched::current_process();
        if (!sched::capable(proc->cred, sched::cap_t::sys_tty_config))
            return -EPERM;

        std::shared_ptr<sched::ctty_base> ctty;
        {
            auto locked = proc->session->ctty.lock();
            ctty = locked.value();
        }
        if (ctty)
            ctty->detach(proc->session.get());
        return 0;
    }

    std::ssize_t getrandom(void __user *buf, std::size_t buflen, unsigned int flags)
    {
        constexpr unsigned int grnd_nonblock = 0x0001;
        constexpr unsigned int grnd_random = 0x0002;
        constexpr unsigned int grnd_insecure = 0x0004;

        if (flags & ~(grnd_nonblock | grnd_random | grnd_insecure))
            return -EINVAL;

        if ((flags & grnd_random) && (flags & grnd_insecure))
            return -EINVAL;

        if (buflen == 0)
            return 0;

        auto uspan = lib::maybe_uspan<std::byte>::create(buf, buflen);
        if (!uspan)
            return -EFAULT;

        const auto res = random::get_bytes(*uspan);
        if (res < 0)
            return -EFAULT;
        return res;
    }

    int prctl(
        int option, unsigned long arg2, unsigned long arg3,
        unsigned long arg4, unsigned long arg5
    )
    {
        lib::unused(arg3, arg4, arg5);
        switch (option)
        {
            case 3: // PR_GET_DUMPABLE
                return static_cast<int>(
                    sched::current_process()->dumpable.load(std::memory_order_relaxed)
                );
            case 4: // PR_SET_DUMPABLE
                if (arg2 != static_cast<unsigned long>(sched::dumpable_t::disable) &&
                    arg2 != static_cast<unsigned long>(sched::dumpable_t::user))
                    return -EINVAL;

                sched::current_process()->dumpable.store(
                    static_cast<sched::dumpable_t>(arg2), std::memory_order_relaxed
                );
                return 0;
            case 23: // PR_CAPBSET_READ
            {
                if (arg2 >= 64)
                    return -EINVAL;
                const auto cap = static_cast<sched::cap_t>(1ul << arg2);
                if (!sched::cap_valid(cap))
                    return -EINVAL;
                return sched::has_cap(sched::current_process()->cred->bounding, cap);
            }
            default:
                lib::error("prctl: unhandled option: 0x{:X}", option);
                return -EINVAL;
        }
    }

    int syslog(int type, char __user *buf, int len)
    {
        // TODO
        lib::unused(type, buf, len);
        return -ENOSYS;
    }
} // namespace syscall::misc
