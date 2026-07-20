// Copyright (C) 2024-2026  ilobilo

module system.vfs.socket;

import system.sched.wait_queue;
import system.sched.mutex;
import system.sched;
import drivers.fs.procfs;
import system.dev;
import fmt;

namespace vfs::socket::netlink
{
    namespace
    {
        constexpr std::size_t default_rcvbuf = lib::kib(208);
        constexpr std::size_t min_rcvbuf = lib::kib(4);
        constexpr std::size_t max_rcvbuf = lib::mib(16);

        struct netlink_sock_t;
        void add_netlink_sock(netlink_sock_t *sock);
        void remove_netlink_sock(netlink_sock_t *sock);

        struct table_t;
        struct netlink_sock_t : socket_t, std::enable_shared_from_this<netlink_sock_t>
        {
            lib::intrusive_list_hook<netlink_sock_t> proto_hook;

            table_t *const table;

            sched::wait_queue_t read_wait;
            sched::wait_queue_t write_wait;

            struct state_t
            {
                std::uint32_t portid = 0;
                std::uint32_t groups = 0;

                std::uint32_t dest_portid = 0;
                std::uint32_t dest_groups = 0;

                bool bound = false;
                bool connected = false;
                bool passcred = false;
                bool overrun = false;

                timeval rcvtimeo { };
                timeval sndtimeo { };
            };
            lib::locker<state_t, sched::mutex_t> state;

            struct dgram_t
            {
                std::uint32_t src_portid;
                std::uint32_t dst_group;
                std::shared_ptr<lib::membuffer> payload;
                std::optional<ucred> cred;
            };

            struct receive_t
            {
                lib::list<dgram_t> queue;
                std::size_t bytes = 0;
                std::size_t rcvbuf = default_rcvbuf;
            };
            lib::locker<receive_t, sched::mutex_t> receive;

            netlink_sock_t(table_t *table, int protocol, sock_type type)
                : socket_t { protocol, af_netlink, type }, proto_hook { }, table { table }
            {
                add_netlink_sock(this);
            }

            ~netlink_sock_t()
            {
                remove_netlink_sock(this);
            }

            bool deliver(
                std::uint32_t src_portid, std::uint32_t dst_group,
                const std::shared_ptr<lib::membuffer> &payload,
                const std::optional<ucred> &cred
            )
            {
                const auto size = payload->size();
                {
                    auto locked = receive.lock();
                    if (locked->bytes + size > locked->rcvbuf)
                    {
                        state.lock()->overrun = true;
                        return false;
                    }

                    locked->queue.emplace_back(src_portid, dst_group, payload, std::move(cred));
                    locked->bytes += size;
                }

                read_wait.wake_all();
                return true;
            }

            std::uint32_t alloc_portid();
            lib::expect<void> bind_portid(std::uint32_t portid);
            lib::expect<void> autobind();

            auto bind(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void>;

            auto connect(
                lib::maybe_uspan<const std::byte> addr, bool nonblock
            ) -> lib::expect<void>;

            auto listen(int backlog) -> lib::expect<void>
            {
                lib::unused(backlog);
                return std::unexpected { lib::err::operation_unsupported };
            }

            auto accept(
                lib::maybe_uspan<std::byte> peer_addr_out,
                socklen_t *addr_len_inout, bool nonblock
            ) -> lib::expect<std::shared_ptr<socket_t>>
            {
                lib::unused(peer_addr_out, addr_len_inout, nonblock);
                return std::unexpected { lib::err::operation_unsupported };
            }

            auto sendmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t>;
            auto recvmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t>;

            auto ioctl(std::uint64_t request, lib::uptr_or_addr argp) -> lib::expect<int>
            {
                switch (request)
                {
                    case 0x541B: // FIONREAD
                    {
                        auto locked = receive.lock();
                        const auto count = !locked->queue.empty()
                            ? locked->queue.front().payload->size() : 0;
                        if (!argp.write(count))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    default:
                        return std::unexpected { lib::err::inappropriate_ioctl };
                }
            }

            auto poll(vfs::poll_table_t *pt) -> lib::expect<std::uint16_t>
            {
                if (pt)
                {
                    pt->add(read_wait);
                    pt->add(write_wait);
                }

                std::uint16_t mask = pollout | pollwrnorm;
                if (state.lock()->overrun)
                    mask |= pollerr;
                if (!receive.lock()->queue.empty())
                    mask |= pollin | pollrdnorm;
                return mask;
            }

            auto shutdown(int how) -> lib::expect<void>
            {
                lib::unused(how);
                read_wait.wake_all();
                write_wait.wake_all();
                return { };
            }

            auto getsockname(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t>
            {
                sockaddr_nl sa {
                    .family = af_netlink,
                    .pad = 0,
                    .pid = 0,
                    .groups = 0
                };
                {
                    auto locked = state.lock();
                    sa.pid = locked->portid;
                    sa.groups = locked->groups;
                }

                const auto len = std::min(out.size(), sizeof(sa));
                if (!out.subspan(0, len)
                    .copy_from(std::as_bytes(std::span { &sa, 1 }).subspan(0, len)))
                    return std::unexpected { lib::err::invalid_address };
                return sizeof(sa);
            }

            auto getpeername(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t>
            {
                sockaddr_nl sa {
                    .family = af_netlink,
                    .pad = 0,
                    .pid = 0,
                    .groups = 0
                };
                {
                    auto locked = state.lock();
                    if (!locked->connected)
                        return std::unexpected { lib::err::not_connected };
                    sa.pid = locked->dest_portid;
                    sa.groups = locked->dest_groups;
                }

                const auto len = std::min(out.size(), sizeof(sa));
                if (!out.subspan(0, len)
                    .copy_from(std::as_bytes(std::span { &sa, 1 }).subspan(0, len)))
                    return std::unexpected { lib::err::invalid_address };
                return sizeof(sa);
            }

            auto setsockopt(
                sock_lvl lvl, int opt,
                lib::maybe_uspan<const std::byte> buf
            ) -> lib::expect<void>;
            auto getsockopt(
                sock_lvl lvl, int opt,
                lib::maybe_uspan<std::byte> buf
            ) -> lib::expect<std::size_t>;

            auto release() -> lib::expect<void>;
        };

        struct table_t
        {
            static constexpr std::uint32_t def_autobind = 0xFFFFF000;

            proto_t &proto;
            lib::intrusive_list<
                netlink_sock_t,
                &netlink_sock_t::proto_hook
            > sockets;
            lib::map::flat_hash<
                std::uint32_t,
                netlink_sock_t *
            > by_portid;
            std::uint32_t autobind_rover = def_autobind;
        };

        lib::locker<
            std::array<
                std::unique_ptr<table_t>,
                netlink_max_links
            >, sched::mutex_t
        > registry;

        void add_netlink_sock(netlink_sock_t *sock)
        {
            const auto _ = registry.lock();
            sock->table->sockets.push_back(sock);
        }

        void remove_netlink_sock(netlink_sock_t *sock)
        {
            const auto _ = registry.lock();
            sock->table->sockets.remove(sock);

            if (auto locked = sock->state.lock(); locked->bound)
            {
                const auto it = sock->table->by_portid.find(locked->portid);
                if (it != sock->table->by_portid.end() && it->second == sock)
                    sock->table->by_portid.erase(it);
            }
        }

        struct netlink_family_t : family_t
        {
            netlink_family_t() : family_t { af_netlink } { }

            lib::expect<std::shared_ptr<socket_t>> create(sock_type type, int protocol)
            {
                if (type != sock_raw && type != sock_dgram)
                    return std::unexpected { lib::err::socket_unsupported };

                if (protocol < 0 || protocol >= netlink_max_links)
                    return std::unexpected { lib::err::protocol_unsupported };

                auto table = (*registry.lock())[protocol].get();
                if (!table)
                    return std::unexpected { lib::err::protocol_unsupported };

                return std::make_shared<netlink_sock_t>(table, protocol, type);
            }
        } netlink_family;

        std::shared_ptr<lib::membuffer> make_payload(std::span<const std::byte> src)
        {
            auto buf = std::make_shared<lib::membuffer>(src.size());
            if (!src.empty())
                std::memcpy(buf->data(), src.data(), src.size());
            return buf;
        }

        std::uint32_t group_mask(std::uint32_t group)
        {
            return group ? (1u << (group - 1)) : 0u;
        }

        void broadcast_locked(
            table_t &table, std::uint32_t group,
            const std::shared_ptr<lib::membuffer> &payload,
            std::uint32_t src_portid, const std::optional<ucred> &cred,
            netlink_sock_t *exclude
        )
        {
            const auto bit = group_mask(group);
            for (auto &sock : table.sockets)
            {
                if (&sock == exclude)
                    continue;

                bool member;
                {
                    auto slocked = sock.state.lock();
                    if (slocked->portid == src_portid && src_portid != 0)
                        continue;
                    member = (slocked->groups & bit) != 0;
                }
                if (member)
                    sock.deliver(src_portid, group, payload, std::move(cred));
            }
        }

        std::uint32_t netlink_sock_t::alloc_portid()
        {
            const auto preferred = sched::current_process()->pid;
            if (!table->by_portid.contains(preferred))
                return preferred;

            for (std::size_t tries = 0; tries < 0x100000; tries++)
            {
                const auto cand = table->autobind_rover--;
                if (table->autobind_rover == 0)
                    table->autobind_rover = table_t::def_autobind;
                if (cand != 0 && !table->by_portid.contains(cand))
                    return cand;
            }
            return 0;
        }

        lib::expect<void> netlink_sock_t::bind_portid(std::uint32_t portid)
        {
            auto slocked = state.lock();
            if (slocked->bound)
            {
                if (portid != 0 && portid != slocked->portid)
                    return std::unexpected { lib::err::invalid_argument };
                return { };
            }

            std::uint32_t chosen;
            if (portid != 0)
            {
                if (table->by_portid.contains(portid))
                    return std::unexpected { lib::err::address_in_use };
                chosen = portid;
            }
            else
            {
                chosen = alloc_portid();
                if (chosen == 0)
                    return std::unexpected { lib::err::address_in_use };
            }

            table->by_portid.insert({ chosen, this });
            slocked->portid = chosen;
            slocked->bound = true;
            return { };
        }

        lib::expect<void> netlink_sock_t::autobind()
        {
            {
                if (state.lock()->bound)
                    return { };
            }
            auto locked = registry.lock();
            return bind_portid(0);
        }

        auto netlink_sock_t::bind(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void>
        {
            if (addr.size() < sizeof(sockaddr_nl))
                return std::unexpected { lib::err::invalid_argument };

            sockaddr_nl sa { };
            if (!addr.subspan(0, sizeof(sa)).copy_to(std::as_writable_bytes(std::span { &sa, 1 })))
                return std::unexpected { lib::err::invalid_address };

            if (sa.family != af_netlink)
                return std::unexpected { lib::err::address_family_unsupported };

            const auto valid = table->proto.ngroups < group_max
                ? ((1u << table->proto.ngroups) - 1) : ~0u;
            const auto want_groups = sa.groups & valid;

            if (want_groups && !table->proto.nonroot_recv &&
                !sched::capable(sched::cap_t::net_admin))
                return std::unexpected { lib::err::not_permitted };

            const auto _ = registry.lock();
            if (const auto ret = bind_portid(sa.pid); !ret)
                return ret;

            state.lock()->groups = want_groups;
            return { };
        }

        auto netlink_sock_t::connect(
            lib::maybe_uspan<const std::byte> addr, bool nonblock
        ) -> lib::expect<void>
        {
            lib::unused(nonblock);

            if (addr.size() < sizeof(sockaddr_nl))
                return std::unexpected { lib::err::invalid_argument };

            sockaddr_nl sa { };
            if (!addr.subspan(0, sizeof(sa)).copy_to(std::as_writable_bytes(std::span { &sa, 1 })))
                return std::unexpected { lib::err::invalid_address };

            if (sa.family == af_unspec)
            {
                auto slocked = state.lock();
                slocked->connected = false;
                slocked->dest_portid = 0;
                slocked->dest_groups = 0;
                return { };
            }

            if (sa.family != af_netlink)
                return std::unexpected { lib::err::address_family_unsupported };

            if (const auto ret = autobind(); !ret)
                return ret;

            auto slocked = state.lock();
            slocked->connected = true;
            slocked->dest_portid = sa.pid;
            slocked->dest_groups = sa.groups;
            return { };
        }

        auto netlink_sock_t::sendmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t>
        {
            if (flags & msg_oob)
                return std::unexpected { lib::err::operation_unsupported };

            std::size_t total = 0;
            for (const auto &iov : hdr.iovs)
                total += iov.size();
            if (total == 0)
                return 0uz;
            if (total > max_rcvbuf)
                return std::unexpected { lib::err::message_too_long };

            std::uint32_t dest_portid;
            std::uint32_t dest_groups;
            if (!hdr.name.empty())
            {
                if (hdr.name.size() < sizeof(sockaddr_nl))
                    return std::unexpected { lib::err::invalid_argument };

                sockaddr_nl sa { };
                if (!hdr.name.subspan(0, sizeof(sa))
                    .copy_to(std::as_writable_bytes(std::span { &sa, 1 })))
                    return std::unexpected { lib::err::invalid_address };

                if (sa.family != af_netlink)
                    return std::unexpected { lib::err::address_family_unsupported };

                dest_portid = sa.pid;
                dest_groups = sa.groups;
            }
            else
            {
                auto slocked = state.lock();
                dest_portid = slocked->dest_portid;
                dest_groups = slocked->dest_groups;
            }

            if (const auto ret = autobind(); !ret)
                return std::unexpected { ret.error() };

            const std::uint32_t dest_group = dest_groups
                ? std::countr_zero(dest_groups) + 1 : 0;

            if (dest_group)
            {
                if (dest_group > table->proto.ngroups)
                    return std::unexpected { lib::err::invalid_argument };

                if (!table->proto.nonroot_send && !sched::capable(sched::cap_t::net_admin))
                    return std::unexpected { lib::err::not_permitted };
            }

            auto payload = std::make_shared<lib::membuffer>(total);
            {
                std::size_t off = 0;
                for (const auto &iov : hdr.iovs)
                {
                    if (!iov.copy_to({ payload->data() + off, iov.size() }))
                        return std::unexpected { lib::err::invalid_address };
                    off += iov.size();
                }
            }

            const auto proc = sched::current_process();
            ucred cred {
                .pid = proc->pid,
                .uid = proc->cred->euid,
                .gid = proc->cred->egid
            };

            std::uint32_t self_portid;
            std::size_t wait_ns;
            {
                const auto slocked = state.lock();
                self_portid = slocked->portid;
                wait_ns = slocked->sndtimeo.to_ns();
            }

            if (dest_group != 0)
            {
                const auto _ = registry.lock();
                broadcast_locked(*table, dest_group, payload, self_portid, cred, this);
                return total;
            }

            if (dest_portid == 0)
            {
                table->proto.input(
                    self_portid, { payload->data(), payload->size() },
                    [this](std::span<const std::byte> data) -> lib::expect<void> {
                        const ucred kcred { .pid = 0, .uid = 0, .gid = 0 };
                        if (!deliver(0, 0, make_payload(data), kcred))
                            return std::unexpected { lib::err::no_buffer_space };
                        return { };
                    }
                );
                return total;
            }

            while (true)
            {
                std::size_t gen = 0;
                std::shared_ptr<netlink_sock_t> keepalive;
                {
                    auto locked = registry.lock();
                    const auto it = table->by_portid.find(dest_portid);
                    if (it == table->by_portid.end())
                        return std::unexpected { lib::err::connection_refused };

                    keepalive = it->second->weak_from_this().lock();
                    if (!keepalive)
                        return std::unexpected { lib::err::connection_refused };

                    if (keepalive->deliver(self_portid, 0, payload, cred))
                        return total;

                    gen = keepalive->write_wait.snapshot_gen();
                }

                if (flags & msg_dontwait)
                    return std::unexpected { lib::err::try_again };

                const auto res = keepalive->write_wait.wait_prepared(gen, wait_ns);
                if (res.interrupted || res.killed)
                    return std::unexpected { lib::err::interrupted };
                if (res.expired)
                    return std::unexpected { lib::err::try_again };
            }
        }

        auto netlink_sock_t::recvmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t>
        {
            if (flags & msg_oob)
                return std::unexpected { lib::err::operation_unsupported };

            std::size_t total = 0;
            for (const auto &iov : hdr.iovs)
                total += iov.size();

            bool passcred;
            std::size_t wait_ns;
            {
                auto slocked = state.lock();
                if (slocked->overrun)
                {
                    slocked->overrun = false;
                    return std::unexpected { lib::err::no_buffer_space };
                }
                passcred = slocked->passcred;
                wait_ns = slocked->rcvtimeo.to_ns();
            }

            while (true)
            {
                std::optional<dgram_t> msg;
                std::size_t gen = 0;

                {
                    auto locked = receive.lock();
                    if (!locked->queue.empty())
                    {
                        if (!(flags & msg_peek))
                        {
                            msg = std::move(locked->queue.front());
                            locked->queue.pop_front();
                            locked->bytes -= msg->payload->size();
                        }
                        else msg = locked->queue.front();
                    }
                    else gen = read_wait.snapshot_gen();
                }

                if (msg)
                {
                    const auto payload_size = msg->payload->size();
                    std::size_t out = 0;
                    for (const auto &iov : hdr.iovs)
                    {
                        if (out >= payload_size)
                            break;

                        const auto want = std::min(iov.size(), payload_size - out);
                        if (!iov.copy_from({ msg->payload->data() + out, want }))
                            return std::unexpected { lib::err::invalid_address };
                        out += want;
                    }

                    if (payload_size > total)
                        hdr.out_flags |= msg_trunc;

                    if (!hdr.name.empty())
                    {
                        const sockaddr_nl sa {
                            .family = af_netlink,
                            .pad = 0,
                            .pid = msg->src_portid,
                            .groups = group_mask(msg->dst_group)
                        };
                        const auto copy_len = std::min(hdr.name.size(), sizeof(sa));
                        if (!hdr.name.subspan(0, copy_len)
                            .copy_from(std::as_bytes(std::span { &sa, 1 }).subspan(0, copy_len)))
                            return std::unexpected { lib::err::invalid_address };
                        hdr.addr_len_out = sizeof(sa);
                    }

                    if (passcred && msg->cred.has_value())
                    {
                        const auto span = std::as_bytes(std::span { &*msg->cred, 1 });
                        if (const auto ret = hdr.write_cmsg(scm_credentials, span); !ret)
                            return std::unexpected { ret.error() };
                    }

                    if (!(flags & msg_peek))
                        write_wait.wake_all();

                    return (flags & msg_trunc) ? payload_size : std::min(total, payload_size);
                }

                if (flags & msg_dontwait)
                    return std::unexpected { lib::err::try_again };

                const auto res = read_wait.wait_prepared(gen, wait_ns);
                if (res.interrupted || res.killed)
                    return std::unexpected { lib::err::interrupted };
                if (res.expired)
                    return std::unexpected { lib::err::try_again };
            }
        }

        auto netlink_sock_t::setsockopt(
            sock_lvl lvl, int opt,
            lib::maybe_uspan<const std::byte> buf
        ) -> lib::expect<void>
        {
            const auto read_int = [&](int &out) -> lib::expect<void> {
                if (buf.size() < sizeof(int))
                    return std::unexpected { lib::err::invalid_argument };

                if (!buf.copy_to(std::as_writable_bytes(std::span { &out, 1 })))
                    return std::unexpected { lib::err::invalid_address };
                return { };
            };

            if (lvl == sol_netlink)
            {
                switch (const auto nopt = static_cast<netlink_opt>(opt))
                {
                    case netlink_add_membership:
                    case netlink_drop_membership:
                    {
                        int group;
                        if (const auto ret = read_int(group); !ret)
                            return ret;
                        if (group <= 0 || static_cast<std::uint32_t>(group) > table->proto.ngroups)
                            return std::unexpected { lib::err::invalid_argument };

                        const bool add = nopt == netlink_add_membership;
                        if (add && !table->proto.nonroot_recv &&
                            !sched::capable(sched::cap_t::net_admin))
                            return std::unexpected { lib::err::not_permitted };

                        if (const auto ret = autobind(); !ret)
                            return ret;

                        const auto _ = registry.lock();
                        auto slocked = state.lock();

                        const auto bit = group_mask(group);
                        if (add)
                            slocked->groups |= bit;
                        else
                            slocked->groups &= ~bit;
                        return { };
                    }
                    case netlink_pktinfo:
                    case netlink_broadcast_error:
                    case netlink_no_enobufs:
                    case netlink_cap_ack:
                    case netlink_ext_ack:
                    case netlink_get_strict_chk:
                        // TODO
                        return { };
                    default:
                        return std::unexpected { lib::err::protocol_unsupported };
                }
            }
            else if (lvl != sol_socket)
                return std::unexpected { lib::err::protocol_unsupported };

            if (opt == so_rcvbufforce)
                opt = so_rcvbuf;
            else if (opt == so_sndbufforce)
                opt = so_sndbuf;
            else if (opt == so_attach_filter || opt == so_detach_filter || opt == so_lock_filter)
                return { }; // TODO

            switch (static_cast<sock_opt>(opt))
            {
                case so_rcvbuf:
                case so_sndbuf:
                {
                    int val;
                    if (const auto ret = read_int(val); !ret)
                        return ret;

                    receive.lock()->rcvbuf = std::clamp(
                        std::max<std::size_t>(val, 0uz) * 2,
                        min_rcvbuf, max_rcvbuf
                    );
                    return { };
                }
                case so_passcred:
                {
                    int val;
                    if (const auto ret = read_int(val); !ret)
                        return ret;
                    state.lock()->passcred = val != 0;
                    return { };
                }
                case so_rcvtimeo:
                    if (buf.size() < sizeof(timeval))
                        return std::unexpected { lib::err::invalid_argument };
                    if (!buf.copy_to(std::as_writable_bytes(std::span { &state.lock()->rcvtimeo, 1 })))
                        return std::unexpected { lib::err::invalid_address };
                    return { };
                case so_sndtimeo:
                    if (buf.size() < sizeof(timeval))
                        return std::unexpected { lib::err::invalid_argument };
                    if (!buf.copy_to(std::as_writable_bytes(std::span { &state.lock()->sndtimeo, 1 })))
                        return std::unexpected { lib::err::invalid_address };
                    return { };
                case so_reuseaddr:
                case so_reuseport:
                case so_broadcast:
                case so_dontroute:
                    // TODO
                    return { };
                default:
                    return std::unexpected { lib::err::invalid_argument };
            }
        }

        auto netlink_sock_t::getsockopt(
            sock_lvl lvl, int opt,
            lib::maybe_uspan<std::byte> buf
        ) -> lib::expect<std::size_t>
        {
            const auto copy = [&]<typename Type>(Type val) -> lib::expect<std::size_t> {
                const auto num = std::min(buf.size(), sizeof(Type));
                if (!buf.subspan(0, num)
                    .copy_from(std::as_bytes(std::span { &val, 1 }).subspan(0, num)))
                    return std::unexpected { lib::err::invalid_address };
                return sizeof(Type);
            };

            if (lvl == sol_netlink)
            {
                switch (static_cast<netlink_opt>(opt))
                {
                    case netlink_list_memberships:
                        return copy(state.lock()->groups);
                    default:
                        return std::unexpected { lib::err::protocol_unsupported };
                }
            }

            if (lvl != sol_socket)
                return std::unexpected { lib::err::protocol_unsupported };

            switch (static_cast<sock_opt>(opt))
            {
                case so_type:
                    return copy(static_cast<int>(type));
                case so_domain:
                    return copy(static_cast<int>(family));
                case so_protocol:
                    return copy(protocol);
                case so_passcred:
                    return copy(state.lock()->passcred ? 1 : 0);
                case so_rcvbuf:
                case so_sndbuf:
                    return copy(static_cast<int>(receive.lock()->rcvbuf));
                case so_error:
                {
                    auto slocked = state.lock();
                    const auto err = slocked->overrun ? ENOBUFS : 0;
                    slocked->overrun = false;
                    return copy(err);
                }
                case so_rcvtimeo:
                    if (buf.size() < sizeof(timeval))
                        return std::unexpected { lib::err::invalid_argument };
                    if (!buf.subspan(0, sizeof(timeval))
                        .copy_from(std::as_bytes(std::span { &state.lock()->rcvtimeo, 1 })))
                        return std::unexpected { lib::err::invalid_address };
                    return sizeof(timeval);
                case so_sndtimeo:
                    if (buf.size() < sizeof(timeval))
                        return std::unexpected { lib::err::invalid_argument };
                    if (!buf.subspan(0, sizeof(timeval))
                        .copy_from(std::as_bytes(std::span { &state.lock()->sndtimeo, 1 })))
                        return std::unexpected { lib::err::invalid_address };
                    return sizeof(timeval);
                default:
                    return std::unexpected { lib::err::invalid_argument };
            }
        }

        auto netlink_sock_t::release() -> lib::expect<void>
        {
            {
                const auto _ = registry.lock();
                auto slocked = state.lock();
                if (slocked->bound)
                {
                    const auto it = table->by_portid.find(slocked->portid);
                    if (it != table->by_portid.end() && it->second == this)
                        table->by_portid.erase(it);
                    slocked->bound = false;
                }
                slocked->groups = 0;
            }

            receive.lock()->queue.clear();

            read_wait.wake_all();
            write_wait.wake_all();
            return { };
        }

        void uevent_broadcast(const dev::uevent_t &uev)
        {
            const auto data = fmt::format("{}@{}\0{}\0",
                dev::action_name(uev.action), uev.devpath, fmt::join(uev.envp, "\0"sv)
            );
            lib::unused(broadcast(netlink_kobject_uevent, 1, std::as_bytes(std::span { data }), 0));
        }

        proto_t kobject_uevent {
            netlink_kobject_uevent,
            group_max, true, false
        };
    } // namespace

    lib::expect<void> register_proto(proto_t &proto)
    {
        if (proto.protocol < 0 || proto.protocol >= netlink_max_links)
            return std::unexpected { lib::err::protocol_unsupported };
        if (proto.ngroups > group_max)
            return std::unexpected { lib::err::invalid_argument };

        auto locked = registry.lock();
        if ((*locked)[proto.protocol])
            return std::unexpected { lib::err::already_exists };

        (*locked)[proto.protocol] = std::make_unique<table_t>(proto);
        return { };
    }

    lib::expect<void> broadcast(
        int protocol, std::uint32_t group,
        std::span<const std::byte> payload, std::uint32_t src_portid
    )
    {
        if (protocol < 0 || protocol >= netlink_max_links || group == 0)
            return std::unexpected { lib::err::invalid_argument };

        auto locked = registry.lock();
        auto table = (*locked)[protocol].get();
        if (!table)
            return std::unexpected { lib::err::protocol_unsupported };
        if (group > table->proto.ngroups)
            return std::unexpected { lib::err::invalid_argument };

        const ucred cred {
            .pid = static_cast<pid_t>(src_portid),
            .uid = 0, .gid = 0
        };
        broadcast_locked(
            *table, group, make_payload(payload),
            src_portid, cred, nullptr
        );
        return { };
    }

    lib::expect<void> unicast(
        int protocol, std::uint32_t portid,
        std::span<const std::byte> payload, std::uint32_t src_portid
    )
    {
        if (protocol < 0 || protocol >= netlink_max_links)
            return std::unexpected { lib::err::invalid_argument };

        auto locked = registry.lock();
        auto table = (*locked)[protocol].get();
        if (!table)
            return std::unexpected { lib::err::protocol_unsupported };

        const auto it = table->by_portid.find(portid);
        if (it == table->by_portid.end())
            return std::unexpected { lib::err::connection_refused };

        const ucred cred {
            .pid = static_cast<pid_t>(src_portid),
            .uid = 0, .gid = 0
        };
        if (!it->second->deliver(src_portid, 0, make_payload(payload), cred))
            return std::unexpected { lib::err::no_buffer_space };
        return { };
    }

    lib::initgraph::task netlink_task
    {
        "socket.procfs.register-netlink",
        lib::initgraph::postsched_init_engine,
        lib::initgraph::require { registered_procfs_stage() },
        [] {
            if (const auto ret = register_family(&netlink_family); !ret)
            {
                lib::error(
                    "socket: could not register family 'netlink': {}",
                    lib::error_name(ret.error())
                );
                return;
            }

            if (const auto ret = register_proto(kobject_uevent); !ret)
            {
                lib::error(
                    "socket: could not register netlink protocol 'kobject_uevent': {}",
                    lib::error_name(ret.error())
                );
            }
            else dev::set_uevent_broadcaster(uevent_broadcast);

            using namespace fs::procfs;
            lib::bug_on(!register_per_pid("net/netlink",
                make_file_ops([](auto) {
                    // TODO: rest of the fields
                    std::string out {
                        "sk               Eth Pid        Groups   "
                        "Rmem     Wmem     Dump  Locks    Drops    Inode\n"
                    };

                    auto it = std::back_inserter(out);
                    for (const auto &table : *registry.lock())
                    {
                        if (!table)
                            continue;

                        for (auto &sock : table->sockets)
                        {
                            std::uint32_t portid, groups;
                            {
                                auto slocked = sock.state.lock();
                                portid = slocked->portid;
                                groups = slocked->groups;
                            }
                            const auto rmem = sock.receive.lock()->bytes;
                            fmt::format_to(it,
                                "{:016x} {:<3} {:<10} {:08x} {:<8} {:<8} {:<5} {:<8} {:<8} {:<8}\n",
                                reinterpret_cast<std::uintptr_t>(&sock),
                                sock.protocol, portid, groups, rmem, 0, 0, 0, 0, 0
                            );
                        }
                    }
                    return out;
                }), node_type::file, 0444
            ));
        }
    };
} // namespace vfs::socket::netlink
