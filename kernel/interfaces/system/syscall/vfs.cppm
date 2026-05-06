// Copyright (C) 2024-2026  ilobilo

export module system.syscall.vfs;

import system.sched;
import system.vfs;
import lib;
import std;

export namespace syscall::vfs
{
    using namespace ::vfs;
    lib::expect<path> get_target(
        sched::process_t *proc, int dirfd, const char __user *pathname,
        bool follow_links, bool empty_path, bool automount
    );

    mode_t umask(mode_t mask);

    int openat(int dirfd, const char __user *pathname, int flags, mode_t mode);
    int open(const char __user *pathname, int flags, mode_t mode);
    int creat(const char __user *pathname, mode_t mode);

    int close(int fd);
    int close_range(unsigned int first, unsigned int last, unsigned int flags);

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

    int statx(int dirfd, const char __user *pathname, int flags, unsigned int mask, struct statx __user *statxbuf);

    int faccessat2(int dirfd, const char __user *pathname, int mode, int flags);
    int faccessat(int dirfd, const char __user *pathname, int mode);
    int access(const char __user *pathname, int mode);

    int fchmodat(int dirfd, const char __user *pathname, mode_t mode);
    int fchmodat2(int dirfd, const char __user *pathname, mode_t mode, int flags);
    int chmod(const char __user *pathname, mode_t mode);
    int fchmod(int fd, mode_t mode);

    int fchownat(int dirfd, const char __user *pathname, uid_t owner, gid_t group, int flags);
    int chown(const char __user *pathname, uid_t owner, gid_t group);
    int fchown(int fd, uid_t owner, gid_t group);
    int lchown(const char __user *pathname, uid_t owner, gid_t group);

    std::ssize_t readlinkat(int dirfd, const char __user *pathname, char __user *buf, std::size_t bufsiz);
    std::ssize_t readlink(const char __user *pathname, char __user *buf, std::size_t bufsiz);

    int mkdirat(int dirfd, const char __user *pathname, mode_t mode);
    int mkdir(const char __user *pathname, mode_t mode);

    int unlinkat(int dirfd, const char __user *pathname, int flags);
    int unlink(const char __user *pathname);

    int symlinkat(const char __user *target, int newdirfd, const char __user *linkpath);
    int symlink(const char __user *target, const char __user *linkpath);

    int renameat(int olddirfd, const char __user *oldpath, int newdirfd, const char __user *newpath);
    int rename(const char __user *oldpath, const char __user *newpath);

    int utimensat(int dirfd, const char __user *pathname, const timespec __user *times, int flags);

    int fsync(int fd);

    int truncate(const char __user *pathname, off_t length);
    int ftruncate(int fd, off_t length);

    int ioctl(int fd, unsigned long request, void __user *argp);
    int fcntl(int fd, int cmd, std::uintptr_t arg);

    int dup(int oldfd);
    int dup2(int oldfd, int newfd);
    int dup3(int oldfd, int newfd, int flags);

    int getcwd(char __user *buf, std::size_t size);

    int chdir(const char __user *pathname);
    int fchdir(int fd);

    int pipe2(int __user *pipefd, int flags);
    int pipe(int __user *pipefd);

    int socket(int domain, int type, int protocol);

    int getdents64(int fd, struct dirent64 __user *buf, std::size_t count);

    int fadvise64(int fd, loff_t offset, std::size_t len, int advice);

    struct pollfd
    {
        int fd;
        short events;
        short revents;
    };

    int ppoll(
        pollfd __user *fds, nfds_t nfds, timespec __user *timeout,
        struct sigset_t __user *sigmask
    );
    int poll(pollfd __user *fds, nfds_t nfds, int timeout);

    struct fd_set;
    int select(
        int nfds, fd_set __user *readfds, fd_set __user *writefds,
        fd_set __user *exceptfds, timeval __user *timeout
    );
    int pselect6(
        int nfds, fd_set __user *readfds, fd_set __user *writefds, fd_set __user *exceptfds,
        const timespec __user *timeout, const struct sigset_t __user *sigmask
    );

    int fsopen(const char *fsname, unsigned int flags);

    int inotify_init1(int flags);
} // export namespace syscall::vfs
