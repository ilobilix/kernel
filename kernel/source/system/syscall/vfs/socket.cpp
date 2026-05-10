// Copyright (C) 2024-2026  ilobilo

module system.syscall.vfs;

import system.vfs.socket;
import magic_enum;
import frigg;

namespace syscall::vfs
{
    using namespace ::vfs;

    namespace
    {
        bool is_socket(const std::shared_ptr<vfs::filedesc> &fdesc)
        {
            if (!fdesc->file || !fdesc->file->path.dentry)
                return false;

            const auto &inode = fdesc->file->path.dentry->inode;
            if (!inode)
                return false;

            return inode->stat.type() == stat::s_ifsock;
        }

        auto get_socket(sched::process_t *proc, int sockfd, int *flags = nullptr)
            -> lib::expect<std::shared_ptr<socket::socket_t>>
        {
            const auto fdesc_res = detail::get_fd(proc, sockfd);
            if (!fdesc_res)
                return std::unexpected { fdesc_res.error() };

            const auto &fdesc = *fdesc_res;
            if (!is_socket(fdesc))
                return std::unexpected { lib::err::not_a_socket };

            if (flags)
                *flags = fdesc->file->flags;

            return socket::from_file(*fdesc->file);
        }

        int map_flags(int flags)
        {
            // these are the same but ehh
            int ret = 0;
            if (flags & sock_cloexec)
                ret |= o_closexec;
            if (flags & sock_nonblock)
                ret |= o_nonblock;
            return ret;
        }

        int effective_flags(int fflags)
        {
            int ret = 0;
            if (fflags & o_nonblock)
                ret |= msg_dontwait;
            return ret;
        }

        std::optional<frg::small_vector<
            lib::maybe_uspan<std::byte>, 8,
            frg::allocator<lib::maybe_uspan<std::byte>>
        >> read_iov(const msghdr &kmsg)
        {
            frg::small_vector<
                lib::maybe_uspan<std::byte>, 8,
                frg::allocator<lib::maybe_uspan<std::byte>>
            > vec;
            vec.resize(kmsg.msg_iovlen);

            for (std::size_t i = 0; i < kmsg.msg_iovlen; i++)
            {
                iovec local_iov;
                if (!lib::copy_from_user(&local_iov, kmsg.msg_iov + i, sizeof(iovec)))
                    return std::nullopt;

                auto uspan = lib::maybe_uspan<std::byte>::create(local_iov.iov_base, local_iov.iov_len);
                if (!uspan.has_value())
                    return std::nullopt;

                vec[i] = *uspan;
            }
            return vec;
        }
    } // namespace

    int socket(int domain, int type, int protocol)
    {
        const auto flags = type & ~0xF;
        if (flags & ~(sock_cloexec | sock_nonblock))
            return -EINVAL;

        if (domain < 0 || domain >= af_max)
            return -EAFNOSUPPORT;

        const auto typ = static_cast<sock_type>(type & 0xF);
        if (!magic_enum::enum_contains(typ))
            return -ESOCKTNOSUPPORT;

        auto sres = socket::create(
            static_cast<addr_fam>(domain),
            typ, protocol
        );
        if (!sres)
            return -lib::map_error(sres.error());

        auto res = socket::create_anon(std::move(*sres), map_flags(flags));
        if (!res)
            return -lib::map_error(res.error());

        return *res;
    }

    int connect(int sockfd, const sockaddr __user *addr, socklen_t addrlen)
    {
        if (addrlen < sizeof(addr_fam) || addrlen > sizeof(sockaddr_storage))
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        const auto uspan = lib::maybe_uspan<const std::byte>::create(addr, addrlen);
        if (!uspan)
            return -EFAULT;

        if (const auto res = sock->connect(*uspan); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int accept(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen)
    {
        return accept4(sockfd, addr, addrlen, 0);
    }

    std::ssize_t sendto(
        int sockfd, const void __user *buf, std::size_t len,
        std::uint32_t flags, const sockaddr __user *addr, socklen_t addrlen
    )
    {
        const auto proc = sched::current_process();

        int fflags;
        auto sockres = get_socket(proc, sockfd, &fflags);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        auto bufuspan = lib::maybe_uspan<std::byte>::create(buf, len);
        if (!bufuspan)
            return -EFAULT;

        lib::maybe_uspan<std::byte> nameuspan;
        if (addr)
        {
            auto res = lib::maybe_uspan<std::byte>::create(addr, addrlen);
            if (!res)
                return -EFAULT;

            nameuspan = *res;
        }

        std::span<lib::maybe_uspan<std::byte>> iovs { std::addressof(*bufuspan), 1 };
        socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = { },
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->sendmsg(hdr, flags | effective_flags(fflags));
        if (!res)
            return -lib::map_error(res.error());
        return *res;
    }

    std::ssize_t recvfrom(
        int sockfd, void __user *buf, std::size_t len,
        std::uint32_t flags, sockaddr __user *addr, socklen_t __user *addrlen
    )
    {
        const auto proc = sched::current_process();

        int fflags;
        auto sockres = get_socket(proc, sockfd, &fflags);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        auto bufuspan = lib::maybe_uspan<std::byte>::create(buf, len);
        if (!bufuspan)
            return -EFAULT;

        socklen_t in_len = 0;
        lib::maybe_uspan<std::byte> nameuspan;
        if (addr && addrlen)
        {
            if (!lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
                return -EFAULT;

            auto res = lib::maybe_uspan<std::byte>::create(addr, in_len);
            if (!res)
                return -EFAULT;

            nameuspan = *res;
        }

        std::span<lib::maybe_uspan<std::byte>> iovs { std::addressof(*bufuspan), 1 };
        socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = { },
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->recvmsg(hdr, flags | effective_flags(fflags));
        if (!res)
            return -lib::map_error(res.error());

        if (addrlen && !lib::copy_to_user(addrlen, &hdr.addr_len_out, sizeof(socklen_t)))
            return -EFAULT;
        return *res;
    }

    std::ssize_t sendmsg(int sockfd, const msghdr __user *msg, std::uint32_t flags)
    {
        const auto proc = sched::current_process();

        int fflags;
        auto sockres = get_socket(proc, sockfd, &fflags);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        msghdr kmsg;
        if (!lib::copy_from_user(&kmsg, msg, sizeof(msghdr)))
            return -EFAULT;

        if (kmsg.msg_iovlen > uio_maxiov)
            return -EMSGSIZE;

        lib::maybe_uspan<std::byte> nameuspan;
        if (kmsg.msg_name)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_name, kmsg.msg_namelen);
            if (!res)
                return -EFAULT;
            nameuspan = *res;
        }

        lib::maybe_uspan<std::byte> ctrluspan;
        if (kmsg.msg_control)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_control, kmsg.msg_controllen);
            if (!res)
                return -EFAULT;
            ctrluspan = *res;
        }

        auto vec = read_iov(kmsg);
        if (!vec)
            return -EFAULT;

        std::span<lib::maybe_uspan<std::byte>> iovs { vec->data(), vec->size() };
        socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = ctrluspan,
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->sendmsg(hdr, flags | effective_flags(fflags));
        if (!res)
            return -lib::map_error(res.error());
        return *res;
    }

    std::ssize_t recvmsg(int sockfd, msghdr __user *msg, std::uint32_t flags)
    {
        const auto proc = sched::current_process();

        int fflags;
        auto sockres = get_socket(proc, sockfd, &fflags);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        msghdr kmsg;
        if (!lib::copy_from_user(&kmsg, msg, sizeof(msghdr)))
            return -EFAULT;

        if (kmsg.msg_iovlen > uio_maxiov)
            return -EMSGSIZE;

        lib::maybe_uspan<std::byte> nameuspan;
        if (kmsg.msg_name)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_name, kmsg.msg_namelen);
            if (!res)
                return -EFAULT;
            nameuspan = *res;
        }

        lib::maybe_uspan<std::byte> ctrluspan;
        if (kmsg.msg_control)
        {
            auto res = lib::maybe_uspan<std::byte>::create(kmsg.msg_control, kmsg.msg_controllen);
            if (!res)
                return -EFAULT;
            ctrluspan = *res;
        }

        auto vec = read_iov(kmsg);
        if (!vec)
            return -EFAULT;

        std::span<lib::maybe_uspan<std::byte>> iovs { vec->data(), vec->size() };
        socket::msg_header_t hdr {
            .name = nameuspan,
            .iovs = iovs,
            .msgctrl = ctrluspan,
            .msgctrl_len_out = 0,
            .addr_len_out = 0,
            .out_flags = 0
        };

        const auto res = sock->recvmsg(hdr, flags | effective_flags(fflags));
        if (!res)
            return -lib::map_error(res.error());

        if (!lib::copy_to_user(&msg->msg_namelen, &hdr.addr_len_out, sizeof(socklen_t)))
            return -EFAULT;

        if (!lib::copy_to_user(&msg->msg_controllen, &hdr.msgctrl_len_out, sizeof(socklen_t)))
            return -EFAULT;

        if (!lib::copy_to_user(&msg->msg_flags, &hdr.out_flags, sizeof(int)))
            return -EFAULT;

        return *res;
    }

    int shutdown(int sockfd, int how)
    {
        if (how != shut_rd && how != shut_wr && how != shut_rdwr)
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        if (const auto res = sock->shutdown(how); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int bind(int sockfd, const sockaddr __user *addr, socklen_t addrlen)
    {
        if (addrlen < sizeof(addr_fam) || addrlen > sizeof(sockaddr_storage))
            return -EINVAL;

        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        const auto uspan = lib::maybe_uspan<const std::byte>::create(addr, addrlen);
        if (!uspan)
            return -EFAULT;

        if (const auto res = sock->bind(*uspan); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int listen(int sockfd, int backlog)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        backlog = std::clamp(backlog, 0, somaxconn);
        if (const auto res = sock->listen(backlog); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int getsockname(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len = 0;
        if (!addr || !addrlen || !lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
            return -EFAULT;

        auto uspan = lib::maybe_uspan<std::byte>::create(addr, in_len);
        if (!uspan)
            return -EFAULT;

        const auto res = sock->getsockname(*uspan);
        if (!res)
            return -lib::map_error(res.error());

        const auto actual_len = *res;
        if (!lib::copy_to_user(addrlen, &actual_len, sizeof(socklen_t)))
            return -EFAULT;

        return 0;
    }

    int getpeername(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len = 0;
        if (!addr || !addrlen || !lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
            return -EFAULT;

        auto uspan = lib::maybe_uspan<std::byte>::create(addr, in_len);
        if (!uspan)
            return -EFAULT;

        const auto res = sock->getpeername(*uspan);
        if (!res)
            return -lib::map_error(res.error());

        const auto actual_len = *res;
        if (!lib::copy_to_user(addrlen, &actual_len, sizeof(socklen_t)))
            return -EFAULT;

        return 0;
    }

    int socketpair(int family, int type, int protocol, int __user *sv /* [2] */)
    {
        const auto flags = type & ~0xF;
        if (flags & ~(sock_cloexec | sock_nonblock))
            return -EINVAL;

        if (family < 0 || family >= af_max)
            return -EAFNOSUPPORT;

        const auto typ = static_cast<sock_type>(type & 0xF);
        if (!magic_enum::enum_contains(typ))
            return -ESOCKTNOSUPPORT;

        const auto proc = sched::current_process();

        auto pres = socket::create_pair(
            static_cast<addr_fam>(family),
            typ, protocol
        );
        if (!pres)
            return -lib::map_error(pres.error());

        auto res1 = socket::create_anon(std::move(pres->first), flags);
        if (!res1)
            return -lib::map_error(res1.error());

        auto res2 = socket::create_anon(std::move(pres->second), flags);
        if (!res2)
        {
            proc->fdt->close(*res1);
            return -lib::map_error(res2.error());
        }

        int ksv[2] { *res1, *res2 };
        if (!lib::copy_to_user(sv, ksv, sizeof(int) * 2))
            return -EFAULT;
        return 0;
    }

    int setsockopt(int sockfd, int level, int optname, const char __user *optval, socklen_t optlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        const auto optuspan = lib::maybe_uspan<const std::byte>::create(optval, optlen);
        if (!optuspan)
            return -EFAULT;

        const auto lvl = static_cast<sock_lvl>(level);
        if (const auto res = sock->setsockopt(lvl, optname, *optuspan); !res)
            return -lib::map_error(res.error());
        return 0;
    }

    int getsockopt(int sockfd, int level, int optname, char __user *optval, socklen_t __user *optlen)
    {
        const auto proc = sched::current_process();

        auto sockres = get_socket(proc, sockfd);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len;
        if (!lib::copy_from_user(&in_len, optlen, sizeof(socklen_t)))
            return -EFAULT;

        auto optuspan = lib::maybe_uspan<std::byte>::create(optval, in_len);
        if (!optuspan)
            return -EFAULT;

        const auto lvl = static_cast<sock_lvl>(level);
        const auto res = sock->getsockopt(lvl, optname, *optuspan);
        if (!res)
            return -lib::map_error(res.error());

        const auto actual_len = *res;
        if (!lib::copy_to_user(optlen, &actual_len, sizeof(socklen_t)))
            return -EFAULT;
        return 0;
    }

    int accept4(int sockfd, sockaddr __user *addr, socklen_t __user *addrlen, int flags)
    {
        if (flags & ~(sock_cloexec | sock_nonblock))
            return -EINVAL;

        const auto proc = sched::current_process();

        int fflags;
        auto sockres = get_socket(proc, sockfd, &fflags);
        if (!sockres)
            return -lib::map_error(sockres.error());
        auto sock = std::move(*sockres);

        socklen_t in_len = 0;
        lib::maybe_uspan<std::byte> uspan;
        if (addr && addrlen)
        {
            if (!lib::copy_from_user(&in_len, addrlen, sizeof(socklen_t)))
                return -EFAULT;

            auto res = lib::maybe_uspan<std::byte>::create(addr, in_len);
            if (!res)
                return -EFAULT;

            uspan = *res;
        }

        socklen_t out_len = in_len;
        auto ares = sock->accept(uspan, &out_len, fflags & o_nonblock);
        if (!ares)
            return -lib::map_error(ares.error());

        auto res = socket::create_anon(std::move(*ares), map_flags(flags));
        if (!res)
            return -lib::map_error(res.error());

        if (addr && addrlen)
        {
            if (!lib::copy_to_user(addrlen, &out_len, sizeof(socklen_t)))
                return -EFAULT;
        }

        return *res;
    }
} // namespace syscall::vfs
