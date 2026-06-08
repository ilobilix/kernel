// Copyright (C) 2024-2026  ilobilo

export module system.vfs.socket:unix;

import :base;
import lib;

export {
    struct sockaddr_un
    {
        addr_fam sun_family;
        char sun_path[108];
    };

    struct ucred
    {
        pid_t pid;
        uid_t uid;
        gid_t gid;
    };
} // export

export namespace vfs::socket
{ } // export namespace vfs::socket
