// Copyright (C) 2024-2025  ilobilo

module system.syscall.misc;

import lib;
import cppstd;

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
        lib::copy_to_user(buf, &kbuf, sizeof(utsname));
        return 0;
    }
} // namespace syscall::misc