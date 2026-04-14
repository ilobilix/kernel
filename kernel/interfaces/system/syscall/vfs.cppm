// Copyright (C) 2024-2025  ilobilo

module;

#include <user.h>

export module system.syscall.vfs;

import lib;
import cppstd;

export namespace syscall::vfs
{
    int open(const char __user *pathname, int flags, mode_t mode);
    int creat(const char __user *pathname, mode_t mode);
    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode);

    int close(int fd);

    std::ssize_t read(int fd, void __user *buf, std::size_t count);
    std::ssize_t write(int fd, const void __user *buf, std::size_t count);

    off_t lseek(int fd, off_t offset, int whence);
} // export namespace syscall::vfs