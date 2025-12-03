// Copyright (C) 2024-2025  ilobilo

export module system.syscall.memory;

import lib;
import std;

export namespace syscall::memory
{
    void *mmap(void *addr, std::size_t length, int prot, int flags, int fd, off_t offset);
    int munmap(void *addr, std::size_t length);
    int mprotect(void *addr, std::size_t len, int prot);

    void *brk(void *addr);
} // export namespace syscall::memory