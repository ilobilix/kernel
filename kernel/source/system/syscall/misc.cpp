// Copyright (C) 2024-2025  ilobilo

module system.syscall.misc;

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