// Copyright (C) 2024-2026  ilobilo

export module system.syscall.memory;

import lib;
import std;

export namespace syscall::memory
{
    void *mmap(void *addr, std::size_t length, int prot, int flags, int fd, off_t offset);
    int munmap(void *addr, std::size_t length);
    int mprotect(void *addr, std::size_t len, int prot);
    void *mremap(
        void *old_addr, std::size_t old_size, std::size_t new_size, int flags, void *new_addr
    );

    void *brk(void *addr);

    int mincore(std::size_t start, std::size_t len, unsigned char __user *vec);

    int madvise(void *addr, std::size_t length, int advice);
} // export namespace syscall::memory
