// Copyright (C) 2024-2025  ilobilo

export module system.syscall.vfs;

import lib;
import std;

export namespace syscall::vfs
{
    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode);
    int open(const char __user *pathname, int flags, mode_t mode);
    int creat(const char __user *pathname, mode_t mode);

    int close(int fd);

    std::ssize_t read(int fd, void __user *buf, std::size_t count);
    std::ssize_t write(int fd, const void __user *buf, std::size_t count);

    std::ssize_t pread(int fd, void __user *buf, std::size_t count, off_t offset);
    std::ssize_t pwrite(int fd, const void __user *buf, std::size_t count, off_t offset);

    std::ssize_t readv(int fd, const struct iovec __user *iov, int iovcnt);
    std::ssize_t writev(int fd, const struct iovec __user *iov, int iovcnt);

    std::ssize_t preadv(int fd, const struct iovec __user *iov, int iovcnt, off_t offset);
    std::ssize_t pwritev(int fd, const struct iovec __user *iov, int iovcnt, off_t offset);

    off_t lseek(int fd, off_t offset, int whence);

    int fstatat(int dirfd, const char __user *pathname, stat __user *statbuf, int flags);
    int stat(const char __user *pathname, struct stat __user *statbuf);
    int fstat(int fd, struct stat __user *statbuf);
    int lstat(const char __user *pathname, struct stat __user *statbuf);

    int faccessat(int dirfd, const char __user *pathname, int mode, int flags);
    int access(const char __user *pathname, int mode);

    int ioctl(int fd, unsigned long request, void __user *argp);
    int fcntl(int fd, int cmd, std::uintptr_t arg);

    int dup(int oldfd);
    int dup2(int oldfd, int newfd);
    int dup3(int oldfd, int newfd, int flags);

    char *getcwd(char __user *buf, std::size_t size);

    int pipe2(int __user *pipefd, int flags);
    int pipe(int __user *pipefd);

    int socket(int domain, int type, int protocol);
} // export namespace syscall::vfs