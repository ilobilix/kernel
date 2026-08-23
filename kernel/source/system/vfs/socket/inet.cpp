// Copyright (C) 2024-2026  ilobilo

module;

#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <lwip/api.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/inet_chksum.h>

module system.vfs.socket;

import system.sched.wait_queue;
import system.sched.mutex;
import system.sched;
import system.chrono;
import drivers.fs.procfs;
import drivers.dev.net;
import lwip;
import fmt;

// TODO: inet6

namespace vfs::socket::inet
{
    namespace
    {
        using namespace lib::ioc;
        enum ioctls : std::uint32_t
        {
            siocgstamp_old = 0x8906,
            siocgstampns_old = 0x8907,
            // siocgstamp_new = make_ior<long long[2]>(0x89, 0x06),
            // siocgstampns_new = make_ior<long long[2]>(0x89, 0x07),
            siocaddrt = 0x890B,
            siocdelrt = 0x890C,
            siocgifname = 0x8910,
            siocgifconf = 0x8912,
            siocgifflags = 0x8913,
            siocsifflags = 0x8914,
            siocgifaddr = 0x8915,
            siocsifaddr = 0x8916,
            siocgifdstaddr = 0x8917,
            siocsifdstaddr = 0x8918,
            siocgifbrdaddr = 0x8919,
            siocsifbrdaddrr = 0x891A,
            siocgifnetmask = 0x891B,
            siocsifnetmask = 0x891C,
            siocgifmtu = 0x8921,
            siocsifmtu = 0x8922,
            siocsifhwaddr = 0x8924,
            siocgifhwaddr = 0x8927,
            siocgifindex = 0x8933,
            siocgiftxqlen = 0x8942,
            siocsiftxqlen = 0x8943,
            siocgifmap = 0x8970,
            siocsifmap = 0x8971
        };

        constexpr std::size_t buffer_size = lib::kib(16);
        // TODO
        constexpr std::size_t default_bufsize = lib::kib(64);
        constexpr std::size_t max_dgram = 65535 - 20 - 8;
        constexpr std::size_t max_raw = 65535 - 20;

        constexpr std::uint8_t icmp_echo_reply = 0;
        constexpr std::uint8_t icmp_echo = 8;

        struct icmp_echo_hdr_t
        {
            std::uint8_t type;
            std::uint8_t code;
            std::uint16_t cksum;
            lib::big_u16_t id;
            lib::big_u16_t seq;
        };
        static_assert(sizeof(icmp_echo_hdr_t) == 8);

        struct core_lock_t
        {
            core_lock_t() { LOCK_TCPIP_CORE(); }
            ~core_lock_t() { UNLOCK_TCPIP_CORE(); }

            core_lock_t(const core_lock_t &) = delete;
            core_lock_t &operator=(const core_lock_t &) = delete;
        };

        std::atomic_uint16_t next_ping_id = 32768;
        std::uint16_t alloc_ping_id()
        {
            std::uint16_t id;
            do {
                id = next_ping_id.fetch_add(1, std::memory_order_relaxed);
            } while (id == 0);
            return id;
        }

        constexpr lib::err map_error(err_t err)
        {
            return lwip::from_err(err);
        }

        struct inet_sock_t;
        void register_sock(netconn *nc, std::weak_ptr<inet_sock_t> sock);
        void unregister_sock(netconn *nc);

        void netconn_cb(netconn *nc, netconn_evt evt, std::uint16_t len);

        struct state_data_t
        {
            netconn *nc = nullptr;
            pbuf *rxpb = nullptr;
            std::size_t rxpb_off = 0;

            netbuf *rxnb = nullptr;
            std::size_t rxnb_off = 0;

            std::optional<timespec> rxtime { };
        };

        struct inet_sock_t : socket_impl_t<state_data_t>, std::enable_shared_from_this<inet_sock_t>
        {
            std::atomic_int rcvplus = 0;
            std::atomic_bool sndplus;

            std::atomic<bool> connecting = false;

            std::atomic_uint16_t ping_id = 0;

            bool is_ping() const
            {
                return type == sock_dgram && protocol == ipproto_icmp;
            }

            inet_sock_t(int protocol, sock_type type, netconn *nc)
                : socket_impl_t { protocol, af_inet, type },
                  sndplus { type != sock_stream }
            {
                {
                    auto locked = state.lock();
                    locked->nc = nc;
                    locked->sndbuf = default_bufsize;
                    locked->rcvbuf = default_bufsize;
                }

                if (is_ping())
                    ping_id = alloc_ping_id();

                netconn_set_nonblocking(nc, 1);
            }

            ~inet_sock_t()
            {
                auto locked = state.lock();
                if (locked->rxpb)
                    pbuf_free(locked->rxpb);
                if (locked->rxnb)
                    netbuf_delete(locked->rxnb);

                if (locked->nc)
                {
                    unregister_sock(locked->nc);
                    netconn_delete(locked->nc);
                }
            }

            static auto create(netconn_type nctype, int protocol, sock_type type)
                -> lib::expect<std::shared_ptr<socket_t>>
            {
                auto *nc = netconn_new_with_proto_and_callback(nctype, protocol, netconn_cb);
                if (nc == nullptr)
                    return std::unexpected { lib::err::out_of_memory };

                auto sock = std::make_shared<inet_sock_t>(protocol, type, nc);
                register_sock(nc, sock);
                return sock;
            }

            auto bind(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void> override
            {
                if (addr.size() < sizeof(sockaddr_in))
                    return std::unexpected { lib::err::invalid_argument };

                sockaddr_in sa { };
                if (!addr.first(sizeof(sa)).copy_to(std::as_writable_bytes(std::span { &sa, 1 })))
                    return std::unexpected { lib::err::invalid_address };

                if (sa.family != af_inet)
                    return std::unexpected { lib::err::address_family_unsupported };

                if (is_ping())
                {
                    if (sa.port > 0)
                        ping_id.store(sa.port.load(), std::memory_order_relaxed);
                }
                else if (sa.port > 0 && sa.port < 1024 &&
                    !sched::capable(sched::cap_t::net_bind_service))
                    return std::unexpected { lib::err::not_permitted };

                const ip_addr_t ipa {
                    .u_addr.ip4.addr = sa.addr.addr.data(),
                    .type = IPADDR_TYPE_V4
                };

                const auto locked = state.lock();
                if (const auto ret = netconn_bind(locked->nc, &ipa, sa.port.load()); ret != ERR_OK)
                    return std::unexpected { map_error(ret) };
                return { };
            }

            auto connect(
                lib::maybe_uspan<const std::byte> addr, bool nonblock
            ) -> lib::expect<void> override
            {
                if (addr.size() < sizeof(sockaddr_in))
                    return std::unexpected { lib::err::invalid_argument };

                sockaddr_in sa { };
                if (!addr.first(sizeof(sa)).copy_to(std::as_writable_bytes(std::span { &sa, 1 })))
                    return std::unexpected { lib::err::invalid_address };

                if (sa.family != af_inet)
                    return std::unexpected { lib::err::address_family_unsupported };

                const ip_addr_t ipa {
                    .u_addr.ip4.addr = sa.addr.addr.data(),
                    .type = IPADDR_TYPE_V4
                };

                std::size_t wait_ns;
                {
                    const auto locked = state.lock();
                    wait_ns = locked->sndtimeo.to_ns();
                    connecting.store(true, std::memory_order_release);

                    const auto ret = netconn_connect(locked->nc, &ipa, sa.port.load());
                    if (ret == ERR_OK)
                    {
                        connecting.store(false, std::memory_order_release);
                        return { };
                    }
                    if (ret != ERR_INPROGRESS)
                    {
                        connecting.store(false, std::memory_order_release);
                        return std::unexpected { map_error(ret) };
                    }
                }

                while (true)
                {
                    sched::gen_t gen;
                    {
                        const auto locked = state.lock();
                        if (sndplus.load(std::memory_order_acquire))
                        {
                            connecting.store(false, std::memory_order_release);
                            return { };
                        }

                        if (const auto err = pending_err.load(std::memory_order_acquire);
                            err != errnos { })
                        {
                            connecting.store(false, std::memory_order_release);
                            return std::unexpected { lib::map_error(err) };
                        }

                        gen = write_wait.snapshot_gen();
                    }

                    if (nonblock)
                        return std::unexpected { lib::err::operation_in_progress };

                    const auto res = write_wait.wait_prepared(gen, wait_ns);
                    if (res.aborted())
                    {
                        connecting.store(false, std::memory_order_release);
                        return std::unexpected { lib::err::interrupted };
                    }
                    if (res.expired)
                    {
                        connecting.store(false, std::memory_order_release);
                        return std::unexpected { lib::err::timed_out };
                    }
                }
            }

            auto listen(int backlog) -> lib::expect<void> override
            {
                const auto locked = state.lock();
                if (const auto ret = netconn_listen_with_backlog(locked->nc, backlog); ret != ERR_OK)
                    return std::unexpected { map_error(ret) };

                locked->state = listening;
                return { };
            }

            auto accept(
                lib::maybe_uspan<std::byte> peer_addr_out,
                socklen_t *addr_len_inout, bool nonblock
            ) -> lib::expect<std::shared_ptr<socket_t>> override
            {
                netconn *nnc = nullptr;
                std::size_t wait_ns;
                {
                    const auto locked = state.lock();
                    wait_ns = locked->rcvtimeo.to_ns();
                }

                while (true)
                {
                    sched::gen_t gen;
                    {
                        const auto locked = state.lock();
                        gen = read_wait.snapshot_gen();

                        const auto ret = netconn_accept(locked->nc, &nnc);
                        if (ret == ERR_OK)
                            break;
                        if (ret != ERR_WOULDBLOCK)
                            return std::unexpected { map_error(ret) };
                    }

                    if (nonblock)
                        return std::unexpected { lib::err::try_again };

                    const auto res = read_wait.wait_prepared(gen, wait_ns);
                    if (res.aborted())
                        return std::unexpected { lib::err::interrupted };
                    if (res.expired)
                        return std::unexpected { lib::err::try_again };
                }

                auto client = std::make_shared<inet_sock_t>(protocol, type, nnc);
                register_sock(nnc, client);

                client->sndplus.store(true, std::memory_order_release);
                client->rcvplus.store(nnc->recv_avail > 0, std::memory_order_release);

                if (peer_addr_out.size() > 0 && addr_len_inout)
                {
                    sockaddr_in sa { };
                    sa.family = af_inet;

                    ip_addr_t ip;
                    u16_t port;
                    if (netconn_getaddr(nnc, &ip, &port, 0) == ERR_OK)
                    {
                        sa.port = port;
                        std::memcpy(&sa.addr.addr, &ip.u_addr.ip4.addr, 4);
                    }

                    const auto copy_len = std::min(peer_addr_out.size(), sizeof(sa));
                    if (!peer_addr_out.first(copy_len)
                        .copy_from(std::as_bytes(std::span { &sa, 1 }).first(copy_len)))
                        return std::unexpected { lib::err::invalid_address };
                    *addr_len_inout = sizeof(sa);
                }
                return client;
            }

            auto sendmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> override
            {
                std::size_t wait_ns;
                {
                    const auto locked = state.lock();
                    wait_ns = locked->sndtimeo.to_ns();
                }

                if (type == sock_stream)
                {
                    std::size_t total = 0;
                    for (const auto &iov : hdr.iovs)
                        total += iov.size();

                    if (total == 0)
                        return 0uz;

                    lib::membuffer buf { std::min(total, buffer_size) };
                    const auto lflags = NETCONN_COPY | NETCONN_DONTBLOCK |
                        (flags & msg_more ? NETCONN_MORE : 0);

                    std::size_t total_sent = 0;
                    for (const auto &iov : hdr.iovs)
                    {
                        std::size_t iov_off = 0;
                        while (iov_off < iov.size())
                        {
                            const auto want = std::min(iov.size() - iov_off, buf.size());
                            const auto chunk = buf.span().first(want);
                            if (!iov.subspan(iov_off, want).copy_to(chunk))
                            {
                                if (total_sent > 0)
                                    return total_sent;
                                return std::unexpected { lib::err::invalid_address };
                            }

                            std::size_t sent = 0;
                            while (sent < want)
                            {
                                sched::gen_t gen;
                                std::size_t num_bytes = 0;
                                {
                                    const auto locked = state.lock();
                                    gen = write_wait.snapshot_gen();

                                    auto ret = netconn_write_partly(
                                        locked->nc,
                                        buf.data() + sent,
                                        want - sent,
                                        lflags, &num_bytes
                                    );

                                    if (ret == ERR_OK)
                                    {
                                        sent += num_bytes;
                                        total_sent += num_bytes;

                                        if (sent >= want)
                                            break;
                                    }
                                    else if (ret != ERR_WOULDBLOCK)
                                    {
                                        if (total_sent > 0)
                                            return total_sent;
                                        return std::unexpected { map_error(ret) };
                                    }
                                }

                                if (flags & msg_dontwait)
                                {
                                    if (total_sent > 0)
                                        return total_sent;
                                    return std::unexpected { lib::err::try_again };
                                }

                                const auto res = write_wait.wait_prepared(gen, wait_ns);
                                if (res.aborted())
                                {
                                    if (total_sent > 0)
                                        return total_sent;
                                    return std::unexpected { lib::err::interrupted };
                                }
                                if (res.expired)
                                {
                                    if (total_sent > 0)
                                        return total_sent;
                                    return std::unexpected { lib::err::try_again };
                                }
                            }
                            iov_off += want;
                        }
                    }
                    return total_sent;
                }

                if (type == sock_dgram || type == sock_raw)
                {
                    if (!hdr.name.empty() && hdr.name.size() < sizeof(sockaddr_in))
                        return std::unexpected { lib::err::invalid_argument };

                    const bool has_dest = !hdr.name.empty();

                    ip_addr_t ipa { };
                    u16_t port = 0;
                    if (has_dest)
                    {
                        sockaddr_in sa { };
                        if (!hdr.name.first(sizeof(sa))
                            .copy_to(std::as_writable_bytes(std::span { &sa, 1 })))
                            return std::unexpected { lib::err::invalid_address };

                        if (sa.family != af_inet)
                            return std::unexpected { lib::err::address_family_unsupported };

                        ipa = {
                            .u_addr.ip4.addr = sa.addr.addr.data(),
                            .type = IPADDR_TYPE_V4
                        };
                        port = sa.port.load();
                    }

                    std::size_t total = 0;
                    for (const auto &iov : hdr.iovs)
                        total += iov.size();

                    if (total > (type == sock_raw ? max_raw : max_dgram))
                        return std::unexpected { lib::err::message_too_long };

                    auto *nb = netbuf_new();
                    if (!nb)
                        return std::unexpected { lib::err::out_of_memory };

                    void *ptr = netbuf_alloc(nb, total);
                    if (!ptr)
                    {
                        netbuf_delete(nb);
                        return std::unexpected { lib::err::out_of_memory };
                    }

                    std::size_t off = 0;
                    for (const auto &iov : hdr.iovs)
                    {
                        if (!iov.copy_to({ static_cast<std::byte *>(ptr) + off, iov.size() }))
                        {
                            netbuf_delete(nb);
                            return std::unexpected { lib::err::invalid_address };
                        }
                        off += iov.size();
                    }

                    if (is_ping())
                    {
                        if (total < sizeof(icmp_echo_hdr_t))
                        {
                            netbuf_delete(nb);
                            return std::unexpected { lib::err::invalid_argument };
                        }

                        auto *icmp = std::start_lifetime_as<icmp_echo_hdr_t>(ptr);
                        if (icmp->type != icmp_echo)
                        {
                            netbuf_delete(nb);
                            return std::unexpected { lib::err::invalid_argument };
                        }

                        icmp->id = ping_id.load(std::memory_order_relaxed);
                        icmp->cksum = 0;
                        icmp->cksum = inet_chksum(ptr, total);
                    }

                    while (true)
                    {
                        sched::gen_t gen;
                        {
                            const auto locked = state.lock();
                            gen = write_wait.snapshot_gen();

                            const auto ret = has_dest
                                ? netconn_sendto(locked->nc, nb, &ipa, port)
                                : netconn_send(locked->nc, nb);

                            if (ret == ERR_OK)
                                break;

                            if (ret != ERR_WOULDBLOCK)
                            {
                                netbuf_delete(nb);
                                return std::unexpected { map_error(ret) };
                            }
                        }

                        if (flags & msg_dontwait)
                        {
                            netbuf_delete(nb);
                            return std::unexpected { lib::err::try_again };
                        }

                        const auto res = write_wait.wait_prepared(gen, wait_ns);
                        if (res.aborted())
                        {
                            netbuf_delete(nb);
                            return std::unexpected { lib::err::interrupted };
                        }
                        if (res.expired)
                        {
                            netbuf_delete(nb);
                            return std::unexpected { lib::err::try_again };
                        }
                    }

                    netbuf_delete(nb);
                    return total;
                }
                return std::unexpected { lib::err::protocol_unsupported };
            }

            auto recvmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> override
            {
                std::size_t total = 0;
                for (const auto &iov : hdr.iovs)
                    total += iov.size();

                std::size_t wait_ns;
                {
                    const auto locked = state.lock();
                    wait_ns = locked->rcvtimeo.to_ns();
                }

                if (type == sock_stream)
                {
                    lib::membuffer buffer { std::min(total, buffer_size) };

                    std::size_t received = 0;
                    std::size_t cur_iov = 0;
                    std::size_t iov_off = 0;
                    std::size_t peek_off = 0;

                    while (received < total && cur_iov < hdr.iovs.size())
                    {
                        auto &iov = hdr.iovs[cur_iov];
                        if (iov_off >= iov.size())
                        {
                            cur_iov++;
                            iov_off = 0;
                            continue;
                        }

                        sched::gen_t gen;
                        std::size_t chunk = 0;
                        bool got_data = false;

                        {
                            auto locked = state.lock();
                            gen = read_wait.snapshot_gen();

                            if (!locked->rxpb)
                            {
                                const auto ret = netconn_recv_tcp_pbuf(locked->nc, &locked->rxpb);
                                if (ret == ERR_CLSD)
                                {
                                    locked->shut_read = true;
                                    return received;
                                }
                                if (ret != ERR_OK && ret != ERR_WOULDBLOCK)
                                    return std::unexpected { map_error(ret) };
                            }

                            if (locked->rxpb)
                            {
                                locked->rxtime = chrono::now(chrono::realtime);

                                const auto src_off = locked->rxpb_off +
                                    ((flags & msg_peek) ? peek_off : 0);

                                chunk = std::min({
                                    static_cast<std::size_t>(locked->rxpb->tot_len) - src_off,
                                    iov.size() - iov_off,
                                    buffer.size()
                                });

                                if (chunk > 0)
                                {
                                    const auto tmp = buffer.span().first(chunk);
                                    lib::bug_on(pbuf_copy_partial(
                                        locked->rxpb, tmp.data(), chunk, src_off
                                    ) != chunk);

                                    if (!iov.subspan(iov_off, chunk).copy_from(tmp))
                                        return std::unexpected { lib::err::invalid_address };

                                    got_data = true;
                                    if (!(flags & msg_peek))
                                    {
                                        locked->rxpb_off += chunk;
                                        if (locked->rxpb_off >= locked->rxpb->tot_len)
                                        {
                                            pbuf_free(locked->rxpb);
                                            locked->rxpb = nullptr;
                                            locked->rxpb_off = 0;
                                        }
                                    }
                                    else peek_off += chunk;
                                }
                                else if (flags & msg_peek)
                                    return received;
                            }
                        }

                        if (got_data)
                        {
                            received += chunk;
                            iov_off += chunk;
                            if (!(flags & msg_waitall) && !(flags & msg_peek))
                                return received;
                            continue;
                        }

                        if ((flags & msg_dontwait) || (!(flags & msg_waitall) && received > 0))
                        {
                            if (received > 0)
                                return received;
                            return std::unexpected { lib::err::try_again };
                        }

                        const auto res = read_wait.wait_prepared(gen, wait_ns);
                        if (res.aborted())
                        {
                            if (received > 0)
                                return received;
                            return std::unexpected { lib::err::interrupted };
                        }
                        if (res.expired)
                        {
                            if (received > 0)
                                return received;
                            return std::unexpected { lib::err::try_again };
                        }
                    }
                    return received;
                }

                if (type == sock_dgram || type == sock_raw)
                {
                    lib::membuffer buffer { std::min(std::max(total, 1uz), buffer_size) };

                    while (true)
                    {
                        sched::gen_t gen;
                        bool got_data = false;
                        bool dropped = false;
                        std::size_t nb_len = 0;

                        {
                            auto locked = state.lock();
                            gen = read_wait.snapshot_gen();

                            if (!locked->rxnb)
                            {
                                const auto ret = netconn_recv(locked->nc, &locked->rxnb);
                                if (ret != ERR_OK && ret != ERR_WOULDBLOCK)
                                    return std::unexpected { map_error(ret) };

                                if (locked->rxnb)
                                    locked->rxtime = chrono::now(chrono::realtime);
                            }

                            std::size_t data_off = 0;
                            if (auto *nb = locked->rxnb; nb && is_ping())
                            {
                                std::uint8_t vhl = 0;
                                icmp_echo_hdr_t icmp { };

                                const auto ours = [&] {
                                    if (netbuf_copy_partial(nb, &vhl, 1, 0) != 1)
                                        return false;

                                    if ((data_off = (vhl & 0x0F) * 4uz) < 20)
                                        return false;

                                    if (netbuf_copy_partial(nb, &icmp, sizeof(icmp), data_off) !=
                                        sizeof(icmp))
                                        return false;

                                    if (icmp.type != icmp_echo_reply)
                                        return false;

                                    return icmp.id == ping_id.load(std::memory_order_relaxed);
                                } ();

                                if (!ours)
                                {
                                    netbuf_delete(nb);
                                    locked->rxnb = nullptr;
                                    dropped = true;
                                }
                            }

                            if (auto *nb = dropped ? nullptr : locked->rxnb)
                            {
                                got_data = true;
                                nb_len = netbuf_len(nb) - data_off;

                                std::size_t copied = 0;
                                for (auto &iov : hdr.iovs)
                                {
                                    if (copied >= nb_len)
                                        break;

                                    std::size_t out = 0;
                                    const auto want = std::min(iov.size(), nb_len - copied);
                                    while (out < want)
                                    {
                                        const auto part = std::min(want - out, buffer.size());
                                        const auto tmp = buffer.span().first(part);

                                        lib::bug_on(netbuf_copy_partial(
                                            nb, tmp.data(), part, data_off + copied + out
                                        ) != part);

                                        if (!iov.subspan(out, part).copy_from(tmp))
                                            return std::unexpected { lib::err::invalid_address };
                                        out += part;
                                    }
                                    copied += want;
                                }

                                if (nb_len > total)
                                    hdr.out_flags |= msg_trunc;

                                if (!hdr.name.empty())
                                {
                                    const auto *addr = netbuf_fromaddr(nb);
                                    const std::uint16_t port = type != sock_raw
                                        ? netbuf_fromport(nb) : 0;

                                    sockaddr_in sa { };
                                    sa.family = af_inet;
                                    sa.port = port;
                                    if (addr)
                                        std::memcpy(&sa.addr.addr, &addr->u_addr.ip4.addr, 4);

                                    const auto copy_len = std::min(hdr.name.size(), sizeof(sa));
                                    if (!hdr.name.first(copy_len)
                                        .copy_from(std::as_bytes(std::span { &sa, 1 })
                                        .first(copy_len)))
                                        return std::unexpected { lib::err::invalid_address };

                                    hdr.addr_len_out = sizeof(sa);
                                }

                                if (!(flags & msg_peek))
                                {
                                    netbuf_delete(nb);
                                    locked->rxnb = nullptr;
                                }
                            }
                        }

                        if (got_data)
                            return (flags & msg_trunc) ? nb_len : std::min(total, nb_len);

                        if (dropped)
                            continue;

                        if (flags & msg_dontwait)
                            return std::unexpected { lib::err::try_again };

                        const auto res = read_wait.wait_prepared(gen, wait_ns);
                        if (res.aborted())
                            return std::unexpected { lib::err::interrupted };
                        if (res.expired)
                            return std::unexpected { lib::err::try_again };
                    }
                }
                return std::unexpected { lib::err::protocol_unsupported };
            }

            auto ioctl(std::uint64_t request, lib::uptr_or_addr argp) -> lib::expect<int> override
            {
                switch (request)
                {
                    case 0x541B: // FIONREAD
                    {
                        auto locked = state.lock();

                        int count = 0;
                        if (locked->nc)
                        {
                            SYS_ARCH_DECL_PROTECT(lev);
                            SYS_ARCH_PROTECT(lev);
                            count = locked->nc->recv_avail;
                            SYS_ARCH_UNPROTECT(lev);

                            count = std::max(count, 0);
                        }

                        if (type == sock_stream && locked->rxpb)
                            count += locked->rxpb->tot_len - locked->rxpb_off;
                        else if (type == sock_dgram && locked->rxnb)
                            count += netbuf_len(locked->rxnb);

                        if (!argp.write(count))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case siocgstamp_old:
                    {
                        auto locked = state.lock();
                        if (!locked->rxtime)
                            return std::unexpected { lib::err::not_found };

                        if (!argp.write(locked->rxtime->to_timeval()))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case siocgstampns_old:
                    {
                        auto locked = state.lock();
                        if (!locked->rxtime)
                            return std::unexpected { lib::err::not_found };

                        if (!argp.write(*locked->rxtime))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case siocaddrt:
                    case siocdelrt:
                    {
                        if (!sched::capable(sched::cap_t::net_admin))
                            return std::unexpected { lib::err::not_permitted };

                        rtentry rt;
                        if (!argp.read(rt))
                            return std::unexpected { lib::err::invalid_address };

                        if (rt.dst.addr.addr != 0 || rt.genmask.addr.addr != 0)
                        {
                            // TODO
                            return std::unexpected { lib::err::not_implemented };
                        }

                        ip4_addr_t gwaddr;
                        std::memcpy(&gwaddr.addr, &rt.gateway.addr.addr, sizeof(gwaddr.addr));

                        std::shared_ptr<dev::net::nic_t> nic;
                        if (rt.dev)
                        {
                            auto str = lib::user_string::get(rt.dev);
                            if (!str)
                                return std::unexpected { lib::err::invalid_address };

                            if (str->empty())
                                return std::unexpected { lib::err::no_such_device };

                            nic = dev::net::by_name(*str);
                        }
                        else
                        {
                            const core_lock_t _ { };
                            for (auto &entry : dev::net::all())
                            {
                                auto *nif = static_cast<netif *>(entry->lwip.get());
                                if (!nif)
                                    continue;

                                if (ip4_addr_netcmp(&gwaddr, netif_ip4_addr(nif), netif_ip4_netmask(nif)))
                                {
                                    nic = std::move(entry);
                                    break;
                                }
                            }
                        }

                        if (!nic)
                            return std::unexpected { lib::err::no_such_device };

                        auto *nif = static_cast<netif *>(nic->lwip.get());
                        if (!nif)
                            return std::unexpected { lib::err::no_such_device };

                        const core_lock_t _ { };
                        if (request == siocaddrt)
                        {
                            netif_set_gw(nif, &gwaddr);
                            netif_set_default(nif);
                        }
                        else // siocdelrt
                        {
                            if (netif_default == nif)
                                netif_set_default(nullptr);
                        }
                        return 0;
                    }
                    case siocgifname:
                    {
                        ifreq ifr;
                        if (!argp.read(ifr))
                            return std::unexpected { lib::err::invalid_address };

                        auto nic = dev::net::by_ifindex(ifr.ivalue);
                        if (!nic)
                            return std::unexpected { lib::err::no_such_device };

                        std::memset(ifr.name, 0, sizeof(ifr.name));

                        const auto &name = nic->name();
                        const auto copy_len = std::min(name.size(), sizeof(ifr.name) - 1);
                        std::memcpy(ifr.name, name.data(), copy_len);

                        if (!argp.write(ifr))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case siocgifconf:
                    {
                        ifconf ifc;
                        if (!argp.read(ifc))
                            return std::unexpected { lib::err::invalid_address };

                        auto buf = lib::maybe_uspan<std::byte>::create(ifc.req, ifc.len);
                        if (ifc.req != nullptr && !buf)
                            return std::unexpected { lib::err::invalid_address };

                        const core_lock_t _ { };

                        std::size_t total = 0;
                        for (const auto &nic : dev::net::all())
                        {
                            auto *nif = static_cast<netif *>(nic->lwip.get());
                            if (!nif)
                                continue;

                            if (buf && (total + sizeof(ifreq) > static_cast<std::size_t>(ifc.len)))
                                break;

                            ifreq ifr { };
                            const auto &name = nic->name();
                            const auto len = std::min(name.size(), sizeof(ifr.name) - 1);
                            std::memcpy(ifr.name, name.data(), len);

                            ifr.addr.family = af_inet;
                            ifr.addr.port = 0;

                            const auto ip = ip4_addr_get_u32(netif_ip4_addr(nif));
                            std::memcpy(&ifr.addr.addr.addr, &ip, sizeof(ip));

                            if (buf)
                            {
                                if (!buf->subspan(total, sizeof(ifreq))
                                    .copy_from(std::as_bytes(std::span { &ifr, 1 })))
                                    return std::unexpected { lib::err::invalid_address };
                            }
                            total += sizeof(ifreq);
                        }

                        ifc.len = total;
                        if (!argp.write(ifc))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case siocgifflags:
                    case siocgifaddr:
                    case siocgifdstaddr:
                    case siocgifbrdaddr:
                    case siocgifnetmask:
                    case siocgifmtu:
                    case siocgifhwaddr:
                    case siocgifindex:
                    case siocgiftxqlen:
                    case siocgifmap:
                    {
                        ifreq ifr;
                        if (!argp.read(ifr))
                            return std::unexpected { lib::err::invalid_address };

                        const auto len = std::strnlen(ifr.name, sizeof(ifr.name));
                        if (len == 0)
                            return std::unexpected { lib::err::no_such_device };

                        auto nic = dev::net::by_name({ ifr.name, len });
                        if (!nic)
                            return std::unexpected { lib::err::no_such_device };

                        const core_lock_t _ { };
                        switch (request)
                        {
                            case siocgifflags:
                                ifr.flags = nic->flags();
                                break;
                            case siocgifaddr:
                            {
                                auto *nif = static_cast<netif *>(nic->lwip.get());
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ifr.addr.family = af_inet;
                                ifr.addr.port = 0;

                                const auto ip = ip4_addr_get_u32(netif_ip4_addr(nif));
                                std::memcpy(&ifr.addr.addr.addr, &ip, sizeof(ip));
                                break;
                            }
                            case siocgifdstaddr:
                            {
                                auto *nif = static_cast<netif *>(nic->lwip.get());
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ifr.dstaddr.family = af_inet;

                                const auto gw = ip4_addr_get_u32(netif_ip4_gw(nif));
                                std::memcpy(&ifr.dstaddr.addr, &gw, sizeof(gw));
                                break;
                            }
                            case siocgifbrdaddr:
                            {
                                auto *nif = static_cast<netif *>(nic->lwip.get());
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ifr.broadaddr.family = af_inet;

                                const auto ip = ip4_addr_get_u32(netif_ip4_addr(nif));
                                const auto mask = ip4_addr_get_u32(netif_ip4_netmask(nif));

                                const auto bcast = ip | ~mask;
                                std::memcpy(&ifr.broadaddr.addr, &bcast, sizeof(bcast));
                                break;
                            }
                            case siocgifnetmask:
                            {
                                auto *nif = static_cast<netif *>(nic->lwip.get());
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ifr.netmask.family = af_inet;

                                const auto mask = ip4_addr_get_u32(netif_ip4_netmask(nif));
                                std::memcpy(&ifr.netmask.addr, &mask, sizeof(mask));
                                break;
                            }
                            case siocgifmtu:
                                ifr.mtu = nic->mtu();
                                break;
                            case siocgifhwaddr:
                                ifr.hwaddr.family = static_cast<addr_fam>(nic->type());
                                std::memcpy(
                                    ifr.hwaddr.data, nic->mac().data(),
                                    std::min(nic->mac().size(), sizeof(ifr.hwaddr.data))
                                );
                                break;
                            case siocgifindex:
                                ifr.ivalue = nic->index();
                                break;
                            case siocgiftxqlen:
                                // TODO
                                ifr.ivalue = 1000;
                                break;
                            case siocgifmap:
                                // TODO
                                std::memset(&ifr.map, 0, sizeof(ifr.map));
                                break;
                            default:
                                std::unreachable();
                        }

                        if (!argp.write(ifr))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case siocsifflags:
                    case siocsifaddr:
                    case siocsifdstaddr:
                    case siocsifbrdaddrr:
                    case siocsifnetmask:
                    case siocsifmtu:
                    case siocsifhwaddr:
                    case siocsiftxqlen:
                    case siocsifmap:
                    {
                        if (!sched::capable(sched::cap_t::net_admin))
                            return std::unexpected { lib::err::not_permitted };

                        ifreq ifr;
                        if (!argp.read(ifr))
                            return std::unexpected { lib::err::invalid_address };

                        const auto len = std::strnlen(ifr.name, sizeof(ifr.name));
                        if (len == 0)
                            return std::unexpected { lib::err::no_such_device };

                        auto nic = dev::net::by_name({ ifr.name, len });
                        if (!nic)
                            return std::unexpected { lib::err::no_such_device };

                        auto *nif = static_cast<netif *>(nic->lwip.get());

                        std::optional<core_lock_t> core;
                        switch (request)
                        {
                            case siocsifaddr:
                            case siocsifdstaddr:
                            case siocsifnetmask:
                                core.emplace();
                                break;
                            default:
                                break;
                        }

                        switch (request)
                        {
                            case siocsifflags:
                            {
                                const auto old_flags = nic->flags();
                                const std::uint32_t new_flags = ifr.flags;

                                if ((new_flags & dev::net::iff_up) && !(old_flags & dev::net::iff_up))
                                {
                                    if (const auto res = nic->up(); !res)
                                        return std::unexpected { res.error() };
                                }
                                else if (!(new_flags & dev::net::iff_up) && (old_flags & dev::net::iff_up))
                                {
                                    if (const auto res = nic->down(); !res)
                                        return std::unexpected { res.error() };
                                }
                                break;
                            }
                            case siocsifaddr:
                            {
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ip4_addr_t ip;
                                std::memcpy(&ip.addr, &ifr.addr.addr.addr, sizeof(ip.addr));
                                netif_set_ipaddr(nif, &ip);
                                break;
                            }
                            case siocsifdstaddr:
                            {
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ip4_addr_t gw;
                                std::memcpy(&gw.addr, &ifr.dstaddr.addr, sizeof(gw.addr));
                                netif_set_gw(nif, &gw);
                                break;
                            }
                            case siocsifbrdaddrr:
                                // TODO
                                break;
                            case siocsifnetmask:
                            {
                                if (!nif)
                                    return std::unexpected { lib::err::no_such_device };

                                ip4_addr_t mask;
                                std::memcpy(&mask.addr, &ifr.netmask.addr, sizeof(mask.addr));
                                netif_set_netmask(nif, &mask);
                                break;
                            }
                            case siocsifmtu:
                            {
                                if (ifr.mtu <= 0)
                                    return std::unexpected { lib::err::invalid_argument };

                                if (const auto res = nic->set_mtu(ifr.mtu); !res)
                                    return std::unexpected { res.error() };
                                break;
                            }
                            case siocsifhwaddr:
                            {
                                dev::net::mac_t mac;
                                std::memcpy(mac.data(), ifr.hwaddr.data, mac.size());

                                if (const auto res = nic->set_mac(mac); !res)
                                    return std::unexpected { res.error() };

                                if (nif)
                                {
                                    std::memcpy(nif->hwaddr, mac.data(), mac.size());
                                    nif->hwaddr_len = mac.size();
                                }
                                break;
                            }
                            case siocsiftxqlen:
                            case siocsifmap:
                                // TODO
                                break;
                            default:
                                std::unreachable();
                        }
                        return 0;
                    }
                    default:
                        lib::error("inet: invalid ioctl 0x{:X}", request);
                        return std::unexpected { lib::err::inappropriate_ioctl };
                }
            }

            auto poll(vfs::poll_table_t *pt) -> lib::expect<std::uint16_t> override
            {
                if (pt)
                {
                    pt->add(read_wait);
                    pt->add(write_wait);
                }

                bool buffered, shut_rd;
                {
                    const auto locked = state.lock();
                    buffered = locked->rxpb != nullptr || locked->rxnb != nullptr;
                    shut_rd = locked->shut_read;
                }

                std::uint16_t mask = 0;
                if (buffered || rcvplus.load(std::memory_order_acquire) > 0 || shut_rd)
                    mask |= pollin  | pollrdnorm;
                if (sndplus.load(std::memory_order_acquire))
                    mask |= pollout | pollwrnorm;
                if (pending_err.load(std::memory_order_acquire) != errnos { } &&
                    !connecting.load(std::memory_order_acquire))
                    mask |= pollerr;
                if (shut_rd)
                    mask |= pollrdhup;
                return mask;
            }

            auto shutdown(int how) -> lib::expect<void> override
            {
                const bool rx = (how == shut_rd || how == shut_rdwr);
                const bool tx = (how == shut_wr || how == shut_rdwr);

                const auto locked = state.lock();
                if (rx)
                    locked->shut_read = true;
                if (tx)
                    locked->shut_write = true;

                if (const auto ret = netconn_shutdown(locked->nc, rx, tx); ret != ERR_OK)
                    return std::unexpected { map_error(ret) };

                read_wait.wake_all();
                write_wait.wake_all();
                return { };
            }

            auto getsockname(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> override
            {
                ip_addr_t ip;
                u16_t port;
                {
                    const auto locked = state.lock();
                    if (const auto ret = netconn_getaddr(locked->nc, &ip, &port, 1); ret != ERR_OK)
                        return std::unexpected { map_error(ret) };
                }

                sockaddr_in sa { };
                sa.family = af_inet;
                sa.port = is_ping() ? ping_id.load(std::memory_order_relaxed) : port;
                std::memcpy(&sa.addr.addr, &ip.u_addr.ip4.addr, 4);

                const auto copy_len = std::min(out.size(), sizeof(sa));
                if (!out.first(copy_len)
                    .copy_from(std::as_bytes(std::span { &sa, 1 }).first(copy_len)))
                    return std::unexpected { lib::err::invalid_address };
                return sizeof(sa);
            }

            auto getpeername(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> override
            {
                ip_addr_t ip;
                u16_t port;
                {
                    const auto locked = state.lock();
                    if (const auto ret = netconn_getaddr(locked->nc, &ip, &port, 0); ret != ERR_OK)
                        return std::unexpected { map_error(ret) };
                }

                sockaddr_in sa { };
                sa.family = af_inet;
                sa.port = port;
                std::memcpy(&sa.addr.addr, &ip.u_addr.ip4.addr, 4);

                const auto copy_len = std::min(out.size(), sizeof(sa));
                if (!out.first(copy_len)
                    .copy_from(std::as_bytes(std::span { &sa, 1 }).first(copy_len)))
                    return std::unexpected { lib::err::invalid_address };
                return sizeof(sa);
            }

            auto do_setsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<const std::byte> buf)
                -> lib::expect<void> override
            {
                if (lvl == static_cast<sock_lvl>(ipproto_tcp))
                {
                    // TODO
                    if (opt == 1) // TCP_NODELAY
                    {
                        int val;
                        if (const auto ret = buf.copy_val_to(val); !ret)
                            return ret;

                        const auto locked = state.lock();
                        if (locked->nc == nullptr)
                            return std::unexpected { lib::err::not_connected };

                        const core_lock_t _ { };
                        auto *pcb = locked->nc->pcb.tcp;
                        if (pcb == nullptr)
                            return std::unexpected { lib::err::not_connected };

                        if (val)
                            tcp_nagle_disable(pcb);
                        else
                            tcp_nagle_enable(pcb);
                        return { };
                    }
                    lib::error("inet: setsockopt ipproto_tcp todo: {}", opt);
                    return { };
                }

                if (lvl == static_cast<sock_lvl>(ipproto_ip))
                {
                    // TODO
                    lib::error("inet: setsockopt ipproto_ip todo: {}", opt);
                    return { };
                }

                return std::unexpected { lib::err::protocol_not_available };
            }

            void sol_sock_finish(core_state_t &core, sock_opt opt) override
            {
                auto &state = static_cast<state_t &>(core);
                switch (opt)
                {
                    case so_reuseaddr:
                    case so_reuseport:
                    {
                        if (state.nc == nullptr)
                            break;

                        const core_lock_t _ { };
                        if (auto *pcb = state.nc->pcb.ip)
                        {
                            if (state.reuseaddr)
                                ip_set_option(pcb, SOF_REUSEADDR);
                            else
                                ip_reset_option(pcb, SOF_REUSEADDR);
                        }
                        break;
                    }
                    case so_broadcast:
                    case so_keepalive:
                    case so_dontroute:
                    case so_rcvbuf:
                    case so_sndbuf:
                        // TODO
                    default:
                        break;
                }
            }

            auto do_getsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<std::byte> buf)
                -> lib::expect<std::size_t> override
            {
                if (lvl == sol_socket)
                    return std::unexpected { lib::err::protocol_not_available };

                if (lvl == static_cast<sock_lvl>(ipproto_tcp))
                {
                    // TODO
                    if (opt == 1) // TCP_NODELAY
                    {
                        const auto locked = state.lock();
                        if (locked->nc == nullptr)
                            return std::unexpected { lib::err::not_connected };

                        const core_lock_t _ { };
                        auto *pcb = locked->nc->pcb.tcp;
                        return buf.copy_val_from<int>(pcb != nullptr && tcp_nagle_disabled(pcb));
                    }
                    return std::unexpected { lib::err::protocol_not_available };
                }

                if (lvl == static_cast<sock_lvl>(ipproto_ip))
                {
                    // TODO
                    if (opt == 1)
                        return buf.copy_val_from<int>(0);
                }
                return std::unexpected { lib::err::protocol_not_available };
            }

            auto release() -> lib::expect<void> override
            {
                const auto locked = state.lock();
                if (!locked->nc)
                    return { };

                unregister_sock(locked->nc);
                const auto ret = netconn_delete(locked->nc);
                locked->nc = nullptr;

                read_wait.wake_all();
                write_wait.wake_all();

                if (ret != ERR_OK)
                    return std::unexpected { map_error(ret) };
                return { };
            }
        };

        lib::locker<
            lib::map::flat_hash<
                netconn *,
                std::weak_ptr<inet_sock_t>
            >, lib::rwspinlock
        > registry;

        void register_sock(netconn *nc, std::weak_ptr<inet_sock_t> sock)
        {
            lib::bug_on(!registry.write_lock()->emplace(nc, sock).second);
        }

        void unregister_sock(netconn *nc)
        {
            lib::bug_on(!registry.write_lock()->erase(nc));
        }

        void netconn_cb(netconn *nc, netconn_evt evt, std::uint16_t len)
        {
            lib::unused(len);

            std::shared_ptr<inet_sock_t> sock;
            {
                const auto locked = registry.read_lock();
                auto it = locked->find(nc);
                if (it == locked->end())
                    return;
                sock = it->second.lock();
            }
            if (!sock)
                return;

            switch (evt)
            {
                case NETCONN_EVT_RCVPLUS:
                    sock->rcvplus.fetch_add(1, std::memory_order_acq_rel);
                    sock->read_wait.wake_all();
                    break;
                case NETCONN_EVT_RCVMINUS:
                    sock->rcvplus.fetch_sub(1, std::memory_order_acq_rel);
                    break;
                case NETCONN_EVT_SENDPLUS:
                    sock->connecting.store(false, std::memory_order_release);
                    sock->sndplus.store(true, std::memory_order_release);
                    sock->write_wait.wake_all();
                    break;
                case NETCONN_EVT_SENDMINUS:
                    sock->sndplus.store(false, std::memory_order_release);
                    break;
                case NETCONN_EVT_ERROR:
                    sock->connecting.store(false, std::memory_order_release);
                    if (const auto err = netconn_err(nc); err != ERR_OK)
                    {
                        sock->pending_err.store(
                            lib::map_error(map_error(err)), std::memory_order_release
                        );
                    }
                    sock->read_wait.wake_all();
                    sock->write_wait.wake_all();
                    break;
            }
        }

        struct inet_family_t : family_t
        {
            inet_family_t() : family_t { af_inet } { }

            lib::expect<std::shared_ptr<socket_t>> create(sock_type type, int protocol)
            {
                switch (type)
                {
                    case sock_stream:
                        if (protocol && protocol != ipproto_tcp)
                            return std::unexpected { lib::err::protocol_unsupported };
                        return inet_sock_t::create(NETCONN_TCP, ipproto_tcp, type);
                    case sock_dgram:
                        if (protocol == ipproto_icmp)
                            return inet_sock_t::create(NETCONN_RAW, ipproto_icmp, type);
                        if (protocol && protocol != ipproto_udp)
                            return std::unexpected { lib::err::protocol_unsupported };
                        return inet_sock_t::create(NETCONN_UDP, ipproto_udp, type);
                    case sock_raw:
                    {
                        if (!sched::capable(sched::cap_t::net_raw))
                            return std::unexpected { lib::err::not_permitted };
                        return inet_sock_t::create(NETCONN_RAW, protocol, type);
                    }
                    default:
                        return std::unexpected { lib::err::socket_unsupported };
                }
            }
        } inet_family;
    } // namespace

    lib::initgraph::task inet_task
    {
        "socket.procfs.register-inet",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { dev::net::registered_stage() },
        [] {
            if (const auto ret = register_family(&inet_family); !ret)
            {
                lib::error(
                    "socket: could not register family 'inet': {}",
                    lib::error_name(ret.error())
                );
                return;
            }
        }
    };
} // namespace vfs::socket::inet
