// Copyright (C) 2024-2026  ilobilo

export module system.syscall.vfs;

import system.sched;
import system.vfs;
import system.vfs.socket;
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
    int close_range(std::uint32_t first, std::uint32_t last, std::uint32_t flags);

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

    int statx(int dirfd, const char __user *pathname, int flags, std::uint32_t mask, struct statx __user *statxbuf);

    int statfs(const char __user *path, struct statfs __user *buf);
    int fstatfs(int fd, struct statfs __user *buf);

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
    int rmdir(const char __user *pathname);

    int mknodat(int dirfd, const char __user *pathname, mode_t mode, dev_t dev);
    int mknod(const char __user *pathname, mode_t mode, dev_t dev);

    int linkat(
        int olddirfd, const char __user *oldpath,
        int newdirfd, const char __user *newpath, int flags
    );
    int link(const char __user *oldpath, const char __user *newpath);

    int symlinkat(const char __user *target, int newdirfd, const char __user *linkpath);
    int symlink(const char __user *target, const char __user *linkpath);

    int renameat(int olddirfd, const char __user *oldpath, int newdirfd, const char __user *newpath);
    int rename(const char __user *oldpath, const char __user *newpath);

    int mount(
        const char __user *source, const char __user *target,
        const char __user *fstype, std::uint64_t flags, const void __user *data
    );

    int setxattr(
        const char __user *pathname, const char __user *name,
        const void __user *value, std::size_t size, int flags
    );
    int lsetxattr(
        const char __user *pathname, const char __user *name,
        const void __user *value, std::size_t size, int flags
    );
    int fsetxattr(
        int fd, const char __user *name,
        const void __user *value, std::size_t size, int flags
    );

    std::ssize_t getxattr(
        const char __user *pathname, const char __user *name,
        void __user *value, std::size_t size
    );
    std::ssize_t lgetxattr(
        const char __user *pathname, const char __user *name,
        void __user *value, std::size_t size
    );
    std::ssize_t fgetxattr(int fd, const char __user *name, void __user *value, std::size_t size);

    std::ssize_t listxattr(const char __user *pathname, char __user *list, std::size_t size);
    std::ssize_t llistxattr(const char __user *pathname, char __user *list, std::size_t size);
    std::ssize_t flistxattr(int fd, char __user *list, std::size_t size);

    int removexattr(const char __user *pathname, const char __user *name);
    int lremovexattr(const char __user *pathname, const char __user *name);
    int fremovexattr(int fd, const char __user *name);

    int utimensat(int dirfd, const char __user *pathname, const timespec __user *times, int flags);

    int fsync(int fd);

    int truncate(const char __user *pathname, off_t length);
    int ftruncate(int fd, off_t length);

    int ioctl(int fd, std::uint64_t request, void __user *argp);
    int fcntl(int fd, int cmd, std::uintptr_t arg);

    int flock(int fd, int operation);

    int dup(int oldfd);
    int dup2(int oldfd, int newfd);
    int dup3(int oldfd, int newfd, int flags);

    int getcwd(char __user *buf, std::size_t size);

    int chdir(const char __user *pathname);
    int fchdir(int fd);

    int pipe2(int __user *pipefd, int flags);
    int pipe(int __user *pipefd);

    int socket(int domain, int type, int protocol);
    int connect(int sockfd, const sockaddr __user *addr, socklen_t addrlen);
    int accept(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen);
    std::ssize_t sendto(
        int sockfd, const void __user *buf, std::size_t len,
        std::uint32_t flags, const sockaddr __user *addr, socklen_t addrlen
    );
    std::ssize_t recvfrom(
        int sockfd, void __user *buf, std::size_t size,
        std::uint32_t flags, sockaddr __user *addr, socklen_t __user *addrlen
    );
    std::ssize_t sendmsg(int sockfd, const msghdr __user *msg, std::uint32_t flags);
    std::ssize_t recvmsg(int sockfd, msghdr __user *msg, std::uint32_t flags);
    int shutdown(int sockfd, int how);
    int bind(int sockfd, const sockaddr __user *addr, socklen_t addrlen);
    int listen(int sockfd, int backlog);
    int getsockname(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen);
    int getpeername(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen);
    int socketpair(int family, int type, int protocol, int __user *sv);
    int setsockopt(int sockfd, int level, int optname, const char __user *optval, socklen_t optlen);
    int getsockopt(int sockfd, int level, int optname, char __user *optval, socklen_t __user *optlen);
    int accept4(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen, int flags);

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

    int fsopen(const char __user *fsname, std::uint32_t flags);

    int inotify_init1(int flags);
} // export namespace syscall::vfs

namespace syscall::vfs::detail
{
    using namespace ::vfs;

    lib::expect<std::shared_ptr<filedesc>> get_fd(sched::process_t *proc, int fdnum);

    lib::expect<path> get_parent(sched::process_t *proc, int dirfd, lib::path_view path);

    lib::expect<resolve_res> resolve_from(
        sched::process_t *proc, int dirfd,
        lib::path_view path, bool automount = true
    );

    lib::expect<path> resolve_parent_dir(
        sched::process_t *proc, int dirfd, lib::path_view path
    );

    lib::expect<lib::path> get_path(const char __user *pathname);

    int close_fd(sched::process_t *proc, int fd, bool was_opened = true);

    std::uint64_t mount_flags(const path &path);
    bool readonly_mount(const path &path);
    bool should_update_atime(const path &path, const kstat &stat, int file_flags = 0);

    int touch_atime(const std::shared_ptr<vfs::file> &file);
} // namespace syscall::vfs::detail
