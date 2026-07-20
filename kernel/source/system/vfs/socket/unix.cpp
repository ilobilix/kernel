// Copyright (C) 2024-2026  ilobilo

module system.vfs.socket;

import drivers.fs.procfs;
import system.chrono;
import system.sched;
import system.vfs;
import fmt;

namespace vfs::socket
{
    namespace
    {
        constexpr std::size_t default_capacity = lib::kib(128);
        constexpr std::size_t min_capacity = lib::kib(4);
        constexpr std::size_t max_capacity = lib::mib(1);

        constexpr std::size_t dgram_cap = lib::kib(208);
        constexpr std::size_t msg_cap = lib::kib(64);

        constexpr std::size_t scm_max_fd = 253;

        std::size_t round_capacity(std::size_t size)
        {
            return std::bit_ceil(std::clamp(size, min_capacity, max_capacity));
        }

        void raise_sigpipe()
        {
            const auto thread = sched::current_thread();
            lib::bug_on(!thread);

            const sched::siginfo_t info {
                .signo = sched::sigpipe,
                .code = sched::si_kernel,
                .err = 0,
                .pid = 0,
                .uid = 0,
                .status = 0,
                .addr = 0,
                .value = 0
            };
            sched::send_signal(thread, info);
        }

        std::size_t copy_out(
            auto &locked, lib::maybe_uspan<std::byte> dst,
            std::size_t off, std::size_t len, bool &fault
        );
        std::size_t copy_in(
            auto &locked, lib::maybe_uspan<std::byte> src,
            std::size_t off, std::size_t len, bool &fault
        );

        struct unix_sock;
        void register_sock(unix_sock *sock);
        void unregister_sock(unix_sock *sock);

        lib::locker<
            lib::map::flat_hash<
                std::string,
                std::weak_ptr<unix_sock>
            >, sched::mutex_t
        > abstract;

        struct sockaddr_un
        {
            addr_fam sun_family;
            char sun_path[108];
        };

        enum state { unconnected, connecting, connected, listening, disconnecting };
        struct unix_sock : socket_t, std::enable_shared_from_this<unix_sock>
        {
            struct private_t
            {
                std::weak_ptr<unix_sock> sock;

                static auto create(std::shared_ptr<unix_sock> sock)
                {
                    return std::make_shared<private_t>(std::move(sock));
                }

                static auto get(const std::shared_ptr<vfs::inode_t> &inode)
                {
                    return std::static_pointer_cast<private_t>(inode->private_data);
                }
            };

            struct ancdata_t
            {
                std::vector<std::shared_ptr<vfs::filedesc>> passed_fds;
                std::optional<ucred> creds;

                bool empty() const { return passed_fds.empty() && !creds.has_value(); }
            };

            sched::wait_queue_t read_wait;
            sched::wait_queue_t write_wait;
            sched::wait_queue_t accept_wait;
            sched::wait_queue_t conn_wait;

            lib::intrusive_list_hook<unix_sock> sockets_hook;

            struct state_t
            {
                state state;

                std::weak_ptr<unix_sock> peer;

                std::shared_ptr<vfs::inode_t> bound_inode;
                std::string bound_path;
                bool bound = false;

                lib::list<std::shared_ptr<unix_sock>> accept_queue;
                std::size_t backlog = 0;

                bool shut_read = false;
                bool shut_write = false;
                bool passcred = false;
                int pending_error = 0;

                std::optional<int> resize_on_con;

                linger linger_opt { };
                timeval rcvtimeo { }, sndtimeo { };

                ucred cred { };

                state_t(enum state state) : state { state } { }
            };
            lib::locker<state_t, sched::mutex_t> state;

            struct receive_t
            {
                lib::membuffer storage;
                std::size_t capacity = 0;
                std::size_t head = 0, tail = 0;
                std::size_t buffered = 0;

                std::size_t total_produced = 0;
                std::size_t total_consumed = 0;

                struct pending_anc
                {
                    std::size_t at_byte;
                    ancdata_t data;
                };
                lib::list<pending_anc> anc_queue;

                struct dgram
                {
                    std::string sender_path;
                    lib::membuffer payload;
                    ancdata_t ancdata;
                };
                lib::list<dgram> dgram_queue;
            };
            lib::locker<receive_t, sched::mutex_t> receive;

            auto parse_ancdata(msg_header_t &hdr) -> lib::expect<ancdata_t>
            {
                ancdata_t anc;
                if (hdr.msgctrl.empty())
                    return anc;

                const auto proc = sched::current_process();
                std::size_t off = 0;

                while (off + sizeof(cmsghdr) <= hdr.msgctrl.size())
                {
                    cmsghdr cmsg { };
                    if (!hdr.msgctrl.subspan(off, sizeof(cmsghdr))
                        .copy_to(std::as_writable_bytes(std::span { &cmsg, 1 })))
                        return std::unexpected { lib::err::invalid_address };

                    if (cmsg.cmsg_len < sizeof(cmsghdr) ||
                        off + cmsg.cmsg_len > hdr.msgctrl.size())
                        return std::unexpected { lib::err::invalid_argument };

                    const auto data_off = off + sizeof(cmsghdr);
                    const auto data_len = cmsg.cmsg_len - sizeof(cmsghdr);

                    if (cmsg.cmsg_level == sol_socket)
                    {
                        if (cmsg.cmsg_type == scm_rights)
                        {
                            if (data_len % sizeof(int) != 0)
                                return std::unexpected { lib::err::invalid_argument };

                            const auto num_fds = data_len / sizeof(int);
                            if (num_fds > scm_max_fd)
                                return std::unexpected { lib::err::invalid_argument };

                            lib::buffer<int> fds { num_fds };
                            if (!hdr.msgctrl.subspan(data_off, data_len)
                                .copy_to(std::as_writable_bytes(fds.span())))
                                return std::unexpected { lib::err::invalid_address };

                            for (const auto fd : fds.span())
                            {
                                auto res = proc->fdt->get(fd);
                                if (!res)
                                    return std::unexpected { lib::err::invalid_fd };
                                anc.passed_fds.push_back(std::move(res));
                            }
                        }
                        else if (cmsg.cmsg_type == scm_credentials)
                        {
                            if (data_len < sizeof(ucred))
                                return std::unexpected { lib::err::invalid_argument };

                            ucred cred { };
                            if (!hdr.msgctrl.subspan(data_off, sizeof(ucred))
                                .copy_to(std::as_writable_bytes(std::span { &cred, 1 })))
                                return std::unexpected { lib::err::invalid_address };
                            anc.creds = cred;
                        }
                        // skip unknown types
                    }

                    off += (cmsg.cmsg_len + cmsg_align - 1) & ~(cmsg_align - 1);
                }

                return anc;
            }

            lib::expect<void> deliver_ancdata(msg_header_t &hdr, ancdata_t &anc, int flags)
            {
                if (anc.empty())
                    return { };

                const auto proc = sched::current_process();
                const bool cloexec = (flags & msg_cmsg_cloexec) != 0;
                const auto max_fd = proc->rlimits->get(sched::rlimit_nofile).cur;

                if (!anc.passed_fds.empty())
                {
                    std::vector<int> new_fds;
                    new_fds.reserve(anc.passed_fds.size());

                    for (auto &fdesc : anc.passed_fds)
                    {
                        auto new_fdesc = std::make_shared<vfs::filedesc>(fdesc->file, cloexec);
                        auto res = proc->fdt->alloc(new_fdesc, 0, false, max_fd);
                        if (!res)
                        {
                            for (const auto fd : new_fds)
                                proc->fdt->close(fd);
                            hdr.out_flags |= msg_ctrunc;
                            return { };
                        }
                        new_fds.push_back(*res);
                    }

                    const auto res = hdr.write_cmsg(scm_rights, std::as_bytes(std::span { new_fds }));
                    if (!res.has_value())
                    {
                        for (const auto fd : new_fds)
                            proc->fdt->close(fd);
                        return std::unexpected { res.error() };
                    }
                    if (!*res)
                    {
                        for (const auto fd : new_fds)
                            proc->fdt->close(fd);
                    }
                }

                if (anc.creds.has_value())
                {
                    std::span span { std::addressof(*anc.creds), 1 };
                    if (const auto ret = hdr.write_cmsg(scm_credentials, std::as_bytes(span)); !ret)
                        return std::unexpected { ret.error() };
                }
                return { };
            }

            void resize_storage(auto &locked, std::size_t new_cap)
            {
                new_cap = round_capacity(new_cap * 2);
                if (new_cap == locked->capacity)
                    return;

                if (locked->buffered > new_cap)
                {
                    const auto diff = locked->buffered - new_cap;
                    locked->tail = (locked->tail + diff) & (locked->capacity - 1);
                    locked->buffered = new_cap;
                }

                lib::membuffer storage { new_cap };
                auto to_copy = locked->buffered;
                auto src = locked->tail, dst = 0uz;

                while (to_copy > 0)
                {
                    const auto chunk = std::min(to_copy, locked->capacity - src);
                    std::memcpy(storage.data() + dst, locked->storage.data() + src, chunk);
                    src = (src + chunk) & (locked->capacity - 1);
                    dst += chunk;
                    to_copy -= chunk;
                }

                locked->storage = std::move(storage);
                locked->capacity = new_cap;
                locked->tail = 0;
                locked->head = locked->buffered;
            }

            unix_sock(
                int protocol, sock_type type, enum state state_ = unconnected,
                std::size_t cap = default_capacity
            ) : socket_t { protocol, af_unix, type }, state { state_ }
            {
                if (type == sock_stream)
                {
                    auto locked = receive.lock();
                    locked->capacity = cap;
                    locked->storage.allocate(locked->capacity);
                }

                {
                    const auto proc = sched::current_process();
                    state.lock()->cred = ucred {
                        .pid = proc->pid,
                        .uid = proc->cred->euid,
                        .gid = proc->cred->egid
                    };
                }

                register_sock(this);
            }

            ~unix_sock() override
            {
                unregister_sock(this);
            }

            auto bind(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void> override
            {
                if (addr.size() < sizeof(addr_fam) || addr.size() > sizeof(sockaddr_un))
                    return std::unexpected { lib::err::invalid_argument };

                sockaddr_un sa { };
                if (!addr.copy_to(std::as_writable_bytes(std::span { &sa, 1 }).subspan(0, addr.size())))
                    return std::unexpected { lib::err::invalid_address };

                if (sa.sun_family != af_unix)
                    return std::unexpected { lib::err::address_family_unsupported };

                std::string_view path;
                bool is_abstract = false;

                if (addr.size() > sizeof(addr_fam))
                {
                    const auto max_len = addr.size() - sizeof(addr_fam);
                    if (sa.sun_path[0] == 0)
                    {
                        is_abstract = true;
                        path = std::string_view { sa.sun_path, max_len };
                    }
                    else
                    {
                        const auto len = std::strnlen(sa.sun_path, max_len);
                        path = std::string_view { sa.sun_path, len };
                    }
                }
                else path = "";

                auto slocked = state.lock();
                if (slocked->bound)
                    return std::unexpected { lib::err::invalid_argument };

                if (slocked->state != unconnected)
                    return std::unexpected { lib::err::invalid_argument };

                if (is_abstract)
                {
                    auto registry = abstract.lock();
                    if (registry->contains(path))
                        return std::unexpected { lib::err::address_in_use };

                    registry->insert({ std::string { path }, shared_from_this() });
                    slocked->bound_path = path;
                    slocked->bound = true;

                    return { };
                }

                if (path.empty())
                {
                    // linux generates abstract names (\0XXXXX)
                    static std::uint32_t autobind_seq = 0;
                    auto registry = abstract.lock();

                    constexpr char hex[] = "0123456789abcdef";
                    for (std::uint32_t retry = 0; retry < 0x100000; retry++)
                    {
                        const auto num = (++autobind_seq) & 0xFFFFFu;
                        std::string name(6, '\0');
                        name[1] = hex[(num >> 16) & 0xF];
                        name[2] = hex[(num >> 12) & 0xF];
                        name[3] = hex[(num >> 8) & 0xF];
                        name[4] = hex[(num >> 4) & 0xF];
                        name[5] = hex[num & 0xF];

                        if (registry->contains(name))
                            continue;

                        registry->insert({ name, shared_from_this() });
                        slocked->bound_path = std::move(name);
                        slocked->bound = true;
                        return { };
                    }
                    return std::unexpected { lib::err::address_in_use };
                }

                const auto proc = sched::current_process();
                const auto ret = vfs::create(
                    proc->vfs->cwd, path, (0777 & ~proc->vfs->umask) | stat::s_ifsock, 0
                );
                if (!ret)
                {
                    if (ret.error() == lib::err::already_exists)
                        return std::unexpected { lib::err::address_in_use };
                    return std::unexpected { ret.error() };
                }

                ret->dentry->inode->private_data = private_t::create(shared_from_this());
                slocked->bound_inode = ret->dentry->inode;
                slocked->bound_path = path;
                slocked->bound = true;

                return { };
            }

            auto connect(lib::maybe_uspan<const std::byte> addr, bool nonblock)
                -> lib::expect<void> override
            {
                if (addr.size() < sizeof(addr_fam) || addr.size() > sizeof(sockaddr_un))
                    return std::unexpected { lib::err::invalid_argument };

                sockaddr_un sa { };
                if (!addr.copy_to(std::as_writable_bytes(std::span { &sa, 1 }).subspan(0, addr.size())))
                    return std::unexpected { lib::err::invalid_address };

                if (sa.sun_family == af_unspec)
                {
                    if (type != sock_dgram)
                        return std::unexpected { lib::err::address_family_unsupported };
                    auto slocked = state.lock();
                    slocked->peer.reset();
                    slocked->state = unconnected;
                    return { };
                }

                if (sa.sun_family != af_unix)
                    return std::unexpected { lib::err::address_family_unsupported };

                std::string_view path;
                bool is_abstract = false;

                if (addr.size() > sizeof(addr_fam))
                {
                    const auto max_len = addr.size() - sizeof(addr_fam);
                    if (sa.sun_path[0] == 0)
                    {
                        is_abstract = true;
                        path = std::string_view { sa.sun_path, max_len };
                    }
                    else
                    {
                        const auto len = std::strnlen(sa.sun_path, max_len);
                        path = std::string_view { sa.sun_path, len };
                    }
                }
                else return std::unexpected { lib::err::not_found };

                const auto proc = sched::current_process();
                std::shared_ptr<unix_sock> target;

                if (is_abstract)
                {
                    auto registry = abstract.lock();
                    auto it = registry->find(path);
                    if (it == registry->end())
                        return std::unexpected { lib::err::connection_refused };

                    target = it->second.lock();
                    if (!target)
                        return std::unexpected { lib::err::connection_refused };
                }
                else
                {
                    const auto res = vfs::resolve(proc->vfs->cwd, path);
                    if (!res)
                        return std::unexpected { res.error() };

                    auto &inode = res->target.dentry->inode;
                    if (inode->stat.type() != stat::s_ifsock)
                        return std::unexpected { lib::err::connection_refused };

                    target = private_t::get(inode)->sock.lock();
                }
                if (!target)
                    return std::unexpected { lib::err::connection_refused };

                if (target->type != type)
                    return std::unexpected { lib::err::wrong_protocol };

                if (type == sock_dgram)
                {
                    auto slocked = state.lock();
                    slocked->peer = target;
                    slocked->state = connected;
                    return { };
                }

                std::size_t wait_ns;
                {
                    auto slocked = state.lock();
                    if (slocked->state == connected)
                        return std::unexpected { lib::err::already_connected };
                    if (slocked->state == connecting)
                        return std::unexpected { lib::err::already_in_progress };
                    if (slocked->state != unconnected)
                        return std::unexpected { lib::err::invalid_argument };

                    slocked->state = connecting;
                    slocked->cred = {
                        .pid = proc->pid,
                        .uid = proc->cred->euid,
                        .gid = proc->cred->egid
                    };
                    wait_ns = slocked->sndtimeo.to_ns();
                }

                {
                    while (true)
                    {
                        std::size_t gen = 0;

                        {
                            auto tlocked = target->state.lock();
                            if (tlocked->state != listening || tlocked->shut_read)
                            {
                                state.lock()->state = unconnected;
                                return std::unexpected { lib::err::connection_refused };
                            }

                            if (tlocked->accept_queue.size() < tlocked->backlog + 1)
                            {
                                std::shared_ptr<unix_sock> server;
                                {
                                    auto slocked = state.lock();
                                    server = std::make_shared<unix_sock>(
                                        protocol, type, connected,
                                        slocked->resize_on_con
                                            ? *slocked->resize_on_con
                                            : default_capacity
                                    );
                                    slocked->resize_on_con = std::nullopt;
                                    slocked->peer = server;
                                    slocked->state = connected;
                                }
                                {
                                    auto sslocked = server->state.lock();
                                    sslocked->peer = shared_from_this();
                                    sslocked->state = connected;
                                    sslocked->bound_path = tlocked->bound_path;
                                    sslocked->passcred = tlocked->passcred;
                                    sslocked->cred = tlocked->cred;
                                }

                                tlocked->accept_queue.push_back(server);
                                target->accept_wait.wake_one();
                                return { };
                            }
                            else gen = target->conn_wait.snapshot_gen();
                        }

                        if (nonblock)
                        {
                            state.lock()->state = unconnected;
                            return std::unexpected { lib::err::try_again };
                        }

                        const auto res = target->conn_wait.wait_prepared(gen, wait_ns);
                        if (res.interrupted || res.killed)
                        {
                            state.lock()->state = unconnected;
                            return std::unexpected { lib::err::interrupted };
                        }
                        if (res.expired)
                        {
                            state.lock()->state = unconnected;
                            return std::unexpected { lib::err::try_again };
                        }
                    }
                }

                return { };
            }

            auto listen(int backlog) -> lib::expect<void> override
            {
                if (type == sock_dgram)
                    return std::unexpected { lib::err::operation_unsupported };

                auto slocked = state.lock();
                if (!slocked->bound)
                    return std::unexpected { lib::err::invalid_argument };

                if (slocked->state != unconnected && slocked->state != listening)
                    return std::unexpected { lib::err::already_connected };

                slocked->backlog = std::clamp(backlog, 0, somaxconn);
                slocked->state = listening;

                const auto proc = sched::current_process();
                slocked->cred = ucred {
                    .pid = proc->pid,
                    .uid = proc->cred->euid,
                    .gid = proc->cred->egid
                };
                return { };
            }

            auto accept(
                lib::maybe_uspan<std::byte> peer_addr_out,
                socklen_t *addr_len_inout, bool nonblock
            ) -> lib::expect<std::shared_ptr<socket_t>> override
            {
                std::size_t wait_ns;
                {
                    const auto slocked = state.lock();
                    wait_ns = slocked->rcvtimeo.to_ns();
                }

                while (true)
                {
                    std::shared_ptr<unix_sock> client;
                    std::size_t gen = 0;
                    bool freed_slot = false;

                    {
                        auto slocked = state.lock();
                        if (slocked->state != listening)
                            return std::unexpected { lib::err::invalid_argument };

                        if (slocked->shut_read)
                            return std::unexpected { lib::err::invalid_argument };

                        if (!slocked->accept_queue.empty())
                        {
                            client = std::move(slocked->accept_queue.front());
                            slocked->accept_queue.pop_front();
                            freed_slot = true;
                        }
                        else gen = accept_wait.snapshot_gen();
                    }

                    if (freed_slot)
                        conn_wait.wake_one();

                    if (client)
                    {
                        sockaddr_un sa { };
                        sa.sun_family = af_unix;
                        socklen_t actual = sizeof(addr_fam);

                        if (auto peer = client->state.lock()->peer.lock())
                        {
                            const auto pslocked = peer->state.lock();
                            if (!pslocked->bound_path.empty())
                            {
                                const bool is_abstract = pslocked->bound_path[0] == 0;
                                const auto copy_size = std::min(
                                    pslocked->bound_path.size(), sizeof(sa.sun_path)
                                );
                                std::memcpy(sa.sun_path, pslocked->bound_path.data(), copy_size);
                                actual = sizeof(addr_fam) +
                                    pslocked->bound_path.size() + !is_abstract;
                            }
                        }

                        if (peer_addr_out.size() > 0 && addr_len_inout)
                        {
                            const auto copy_len = std::min<std::size_t>(peer_addr_out.size(), actual);
                            if (!peer_addr_out.subspan(0, copy_len).copy_from(
                                std::as_bytes(std::span { &sa, 1 }).subspan(0, copy_len)))
                                return std::unexpected { lib::err::invalid_address };
                            *addr_len_inout = actual;
                        }

                        return client;
                    }

                    if (nonblock)
                        return std::unexpected { lib::err::try_again };

                    const auto res = accept_wait.wait_prepared(gen, wait_ns);
                    if (res.interrupted || res.killed)
                        return std::unexpected { lib::err::interrupted };
                    if (res.expired)
                        return std::unexpected { lib::err::try_again };
                }
            }

            auto sendmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> override
            {
                if (flags & msg_oob)
                    return std::unexpected { lib::err::operation_unsupported };

                auto anc_res = parse_ancdata(hdr);
                if (!anc_res)
                    return std::unexpected { anc_res.error() };
                auto anc = std::move(*anc_res);

                std::size_t total = 0;
                for (const auto &iov : hdr.iovs)
                    total += iov.size();

                if (type == sock_stream)
                {
                    if (total == 0)
                        return 0uz;

                    std::shared_ptr<unix_sock> peer_ptr;
                    std::size_t wait_ns;

                    {
                        const auto slocked = state.lock();
                        if (!(peer_ptr = slocked->peer.lock()))
                        {
                            if (!(flags & msg_nosignal))
                                raise_sigpipe();
                            return std::unexpected { lib::err::broken_pipe };
                        }

                        if (slocked->shut_write)
                        {
                            if (!(flags & msg_nosignal))
                                raise_sigpipe();
                            return std::unexpected { lib::err::broken_pipe };
                        }
                        wait_ns = slocked->sndtimeo.to_ns();
                    }

                    if (peer_ptr->state.lock()->passcred && !anc.creds.has_value())
                    {
                        const auto proc = sched::current_process();
                        anc.creds = ucred {
                            .pid = proc->pid,
                            .uid = proc->cred->euid,
                            .gid = proc->cred->egid
                        };
                    }

                    std::size_t sent = 0;
                    std::size_t cur_iov = 0;
                    std::size_t iov_off = 0;
                    bool anc_queued = anc.empty();

                    while (sent < total && cur_iov < hdr.iovs.size())
                    {
                        auto &iov = hdr.iovs[cur_iov];
                        if (iov_off >= iov.size())
                        {
                            cur_iov++;
                            iov_off = 0;
                            continue;
                        }

                        std::size_t chunk = 0;
                        bool fault = false;
                        bool peer_gone = false;
                        std::size_t gen = 0;

                        {
                            {
                                auto slocked = peer_ptr->state.lock();
                                const auto state = slocked->state;
                                if (state == disconnecting || state == unconnected)
                                {
                                    peer_gone = true;
                                    goto skip;
                                }
                            }

                            {
                                auto locked = peer_ptr->receive.lock();
                                const auto available = locked->capacity - locked->buffered;

                                if (available > 0)
                                {
                                    const auto want = std::min(
                                        available, iov.size() - iov_off
                                    );
                                    const auto at_byte = locked->total_produced;
                                    chunk = copy_in(locked, iov, iov_off, want, fault);
                                    if (chunk > 0 && !anc_queued)
                                    {
                                        locked->anc_queue.push_back({
                                            .at_byte = at_byte,
                                            .data = std::move(anc)
                                        });
                                        anc_queued = true;
                                    }
                                }
                                else gen = peer_ptr->write_wait.snapshot_gen();
                            }
                        }

                        skip:
                        if (peer_gone)
                        {
                            if (sent > 0)
                                return sent;

                            if (!(flags & msg_nosignal))
                                raise_sigpipe();
                            return std::unexpected { lib::err::broken_pipe };
                        }

                        if (chunk > 0)
                        {
                            sent += chunk;
                            iov_off += chunk;
                            if (iov_off >= iov.size())
                            {
                                cur_iov++;
                                iov_off = 0;
                            }

                            peer_ptr->read_wait.wake_all();
                            continue;
                        }

                        if (fault)
                        {
                            if (sent > 0)
                                return sent;
                            return std::unexpected { lib::err::invalid_address };
                        }

                        if (flags & msg_dontwait)
                        {
                            if (sent > 0)
                                return sent;
                            return std::unexpected { lib::err::try_again };
                        }

                        const auto res = peer_ptr->write_wait.wait_prepared(gen, wait_ns);
                        if (res.interrupted || res.killed)
                        {
                            if (sent > 0)
                                return sent;
                            return std::unexpected { lib::err::interrupted };
                        }
                        if (res.expired)
                        {
                            if (sent > 0)
                                return sent;
                            return std::unexpected { lib::err::try_again };
                        }
                    }

                    return sent;
                }
                else // sock_dgram and sock_seqpacket
                {
                    if (total > msg_cap)
                        return std::unexpected { lib::err::message_too_long };

                    std::shared_ptr<unix_sock> dest;
                    if (!hdr.name.empty())
                    {
                        sockaddr_un sa { };
                        if (!hdr.name.copy_to(std::as_writable_bytes(std::span { &sa, 1 })
                            .subspan(0, hdr.name.size())))
                            return std::unexpected { lib::err::invalid_address };

                        if (sa.sun_family != af_unix)
                            return std::unexpected { lib::err::address_family_unsupported };

                        const auto max_len = hdr.name.size() - sizeof(addr_fam);
                        if (max_len == 0)
                            return std::unexpected { lib::err::invalid_argument };

                        if (sa.sun_path[0] == '\0')
                        {
                            const std::string_view path { sa.sun_path, max_len };

                            auto registry = abstract.lock();
                            auto it = registry->find(path);
                            if (it == registry->end())
                                return std::unexpected { lib::err::connection_refused };

                            dest = it->second.lock();
                        }
                        else
                        {
                            const auto len = std::strnlen(sa.sun_path, max_len);
                            const std::string_view path { sa.sun_path, len };

                            const auto proc = sched::current_process();
                            const auto res = vfs::resolve(proc->vfs->cwd, path);
                            if (!res)
                                return std::unexpected { res.error() };

                            auto &inode = res->target.dentry->inode;
                            if (inode->stat.type() != stat::s_ifsock)
                                return std::unexpected { lib::err::connection_refused };

                            dest = private_t::get(inode)->sock.lock();
                        }

                        if (!dest)
                            return std::unexpected { lib::err::connection_refused };
                    }
                    else
                    {
                        dest = state.lock()->peer.lock();
                        if (!dest)
                            return std::unexpected { lib::err::not_connected };
                    }

                    if (dest->type != type)
                        return std::unexpected { lib::err::wrong_protocol };

                    if (dest->state.lock()->passcred && !anc.creds.has_value())
                    {
                        const auto proc = sched::current_process();
                        anc.creds = ucred {
                            .pid = proc->pid,
                            .uid = proc->cred->euid,
                            .gid = proc->cred->egid
                        };
                    }

                    lib::membuffer payload { total };
                    std::size_t off = 0;
                    for (const auto &iov : hdr.iovs)
                    {
                        if (!iov.copy_to({ payload.data() + off, iov.size() }))
                            return std::unexpected { lib::err::invalid_address };
                        off += iov.size();
                    }

                    std::string sender_path;
                    std::size_t wait_ns;
                    {
                        auto slocked = state.lock();
                        sender_path = slocked->bound_path;
                        wait_ns = slocked->sndtimeo.to_ns();
                    }

                    while (true)
                    {
                        std::size_t gen = 0;
                        {
                            auto locked = dest->receive.lock();
                            std::size_t queued = 0;
                            for (auto &dgram : locked->dgram_queue)
                                queued += std::max(dgram.payload.size(), 1uz);

                            if (queued <= dgram_cap - std::max(total, 1uz))
                            {
                                locked->dgram_queue.push_back({
                                    std::move(sender_path),
                                    std::move(payload),
                                    std::move(anc)
                                });
                                dest->read_wait.wake_all();
                                return total;
                            }

                            gen = dest->write_wait.snapshot_gen();
                        }

                        if (flags & msg_dontwait)
                            return std::unexpected { lib::err::try_again };

                        const auto res = dest->write_wait.wait_prepared(gen, wait_ns);
                        if (res.interrupted || res.killed)
                            return std::unexpected { lib::err::interrupted };
                        if (res.expired)
                            return std::unexpected { lib::err::try_again };
                    }
                }
            }

            auto recvmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> override
            {
                if (flags & msg_oob)
                    return std::unexpected { lib::err::operation_unsupported };

                std::size_t total = 0;
                for (const auto &iov : hdr.iovs)
                    total += iov.size();

                if (type == sock_stream && total == 0)
                    return 0uz;

                std::size_t wait_ns;
                std::weak_ptr<unix_sock> peer_weak;
                {
                    const auto slocked = state.lock();
                    wait_ns = slocked->rcvtimeo.to_ns();
                    peer_weak = slocked->peer;

                    if (slocked->shut_read)
                        return 0uz;
                }

                if (type == sock_stream)
                {
                    if (flags & msg_peek)
                    {
                        auto locked = receive.lock();

                        std::size_t peek_tail = locked->tail;
                        std::size_t peek_left = locked->buffered;
                        std::size_t peek_out = 0;

                        for (auto &iov : hdr.iovs)
                        {
                            if (peek_left == 0)
                                break;

                            const auto want = std::min(iov.size(), peek_left);
                            const auto first = std::min(want, locked->capacity - peek_tail);
                            const auto rest = want - first;

                            std::span span { locked->storage.data() + peek_tail, first };
                            if (!iov.subspan(0, first).copy_from(span))
                                return std::unexpected { lib::err::invalid_address };
                            peek_tail = (peek_tail + first) & (locked->capacity - 1);

                            if (rest > 0)
                            {
                                std::span span { locked->storage.data(), rest };
                                if (!iov.subspan(first, rest).copy_from(span))
                                    return std::unexpected { lib::err::invalid_address };
                                peek_tail = (peek_tail + rest) & (locked->capacity - 1);
                            }

                            peek_out += want;
                            peek_left -= want;
                        }

                        const auto threshold = locked->total_consumed + peek_out;
                        for (auto &entry : locked->anc_queue)
                        {
                            if (entry.at_byte > threshold)
                                break;
                            if (const auto ret = deliver_ancdata(hdr, entry.data, flags); !ret)
                                return std::unexpected { ret.error() };
                        }

                        return peek_out;
                    }

                    std::size_t received = 0;
                    std::size_t cur_iov = 0;
                    std::size_t iov_off = 0;

                    while (received < total && cur_iov < hdr.iovs.size())
                    {
                        auto &iov = hdr.iovs[cur_iov];
                        if (iov_off >= iov.size())
                        {
                            cur_iov++;
                            iov_off = 0;
                            continue;
                        }

                        std::size_t chunk = 0;
                        bool fault = false;
                        bool eof = false;
                        std::size_t gen = 0;

                        {
                            auto locked = receive.lock();

                            if (locked->buffered > 0)
                            {
                                const auto want = std::min(
                                    locked->buffered, iov.size() - iov_off
                                );
                                chunk = copy_out(locked, iov, iov_off, want, fault);
                            }
                            else
                            {
                                auto peer_ptr = peer_weak.lock();
                                if (peer_ptr)
                                {
                                    const auto pslocked = peer_ptr->state.lock();
                                    if (pslocked->state == disconnecting || pslocked->shut_write)
                                        eof = true;
                                    else
                                        gen = read_wait.snapshot_gen();
                                }
                                else eof = true;
                            }
                        }

                        if (chunk > 0)
                        {
                            received += chunk;
                            iov_off += chunk;
                            if (iov_off >= iov.size())
                            {
                                cur_iov++;
                                iov_off = 0;
                            }

                            {
                                auto locked = receive.lock();
                                while (!locked->anc_queue.empty() &&
                                    locked->anc_queue.front().at_byte <= locked->total_consumed)
                                {
                                    if (const auto ret = deliver_ancdata(
                                        hdr, locked->anc_queue.front().data, flags); !ret)
                                        return std::unexpected { ret.error() };
                                    locked->anc_queue.pop_front();
                                }
                            }

                            if (auto peer_ptr = peer_weak.lock())
                                peer_ptr->write_wait.wake_all();

                            if (!(flags & msg_waitall))
                                return received;
                            continue;
                        }

                        if (eof)
                            return received;

                        if (fault)
                            return std::unexpected { lib::err::invalid_address };

                        if ((flags & msg_dontwait) || (!(flags & msg_waitall) && received > 0))
                        {
                            if (received > 0)
                                return received;
                            return std::unexpected { lib::err::try_again };
                        }

                        const auto res = read_wait.wait_prepared(gen, wait_ns);
                        if (res.interrupted || res.killed)
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
                else // sock_dgram and sock_seqpacket
                {
                    auto peer_weak = state.lock()->peer;
                    while (true)
                    {
                        std::optional<receive_t::dgram> msg;
                        std::size_t gen = 0;

                        {
                            auto locked = receive.lock();
                            if (!locked->dgram_queue.empty())
                            {
                                if (!(flags & msg_peek))
                                {
                                    msg = std::move(locked->dgram_queue.front());
                                    locked->dgram_queue.pop_front();
                                }
                                else msg = locked->dgram_queue.front();
                            }
                            else gen = read_wait.snapshot_gen();
                        }

                        if (msg)
                        {
                            const auto payload_size = msg->payload.size();
                            std::size_t out = 0;

                            for (auto &iov : hdr.iovs)
                            {
                                if (out >= payload_size)
                                    break;

                                const auto want = std::min(iov.size(), payload_size - out);
                                if (!iov.copy_from({ msg->payload.data() + out, want }))
                                    return std::unexpected { lib::err::invalid_address };
                                out += want;
                            }

                            if (payload_size > total)
                                hdr.out_flags |= msg_trunc;

                            if (!hdr.name.empty() && !msg->sender_path.empty())
                            {
                                sockaddr_un sa { };
                                sa.sun_family = af_unix;

                                const bool is_abstract = msg->sender_path[0] == 0;
                                const auto copy_size = std::min(
                                    msg->sender_path.size(), sizeof(sa.sun_path)
                                );
                                std::memcpy(sa.sun_path, msg->sender_path.data(), copy_size);

                                const auto sa_len = sizeof(addr_fam) +
                                    msg->sender_path.size() + !is_abstract;
                                const auto copy_len = std::min(hdr.name.size(), sa_len);

                                if (!hdr.name.subspan(0, copy_len).copy_from(
                                    std::as_bytes(std::span { &sa, 1 }).subspan(0, copy_len)))
                                    return std::unexpected { lib::err::invalid_address };
                                hdr.addr_len_out = sa_len;
                            }

                            if (const auto ret = deliver_ancdata(hdr, msg->ancdata, flags); !ret)
                                return std::unexpected { ret.error() };

                            if (auto peer_ptr = peer_weak.lock())
                                peer_ptr->write_wait.wake_all();

                            return (flags & msg_trunc)
                                ? payload_size
                                : std::min(total, payload_size);
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
            }

            auto ioctl(std::uint64_t request, lib::uptr_or_addr argp) -> lib::expect<int> override
            {
                switch (request)
                {
                    case 0x541B: // FIONREAD
                    {
                        auto locked = receive.lock();
                        int count = (type == sock_stream)
                            ? static_cast<int>(locked->buffered)
                            : !locked->dgram_queue.empty()
                                ? static_cast<int>(locked->dgram_queue.front().payload.size())
                                : 0;

                        if (!argp.write(count))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    case 0x8905: // SIOCATMARK
                    {
                        if (!argp.write(1))
                            return std::unexpected { lib::err::invalid_address };
                        return 0;
                    }
                    default:
                        return std::unexpected { lib::err::inappropriate_ioctl };
                }
            }

            auto poll(vfs::poll_table_t *pt) -> lib::expect<std::uint16_t> override
            {
                if (pt)
                {
                    pt->add(read_wait);
                    pt->add(write_wait);
                    pt->add(accept_wait);
                }

                std::uint16_t mask = 0;

                std::shared_ptr<unix_sock> peer_ptr;
                bool is_listener = false;
                bool is_connected = false;
                bool peer_gone = false;
                {
                    auto slocked = state.lock();
                    peer_ptr = slocked->peer.lock();
                    is_listener = (slocked->state == listening);
                    is_connected = (slocked->state == connected);
                    if (is_connected)
                    {
                        peer_gone = !peer_ptr || [&] {
                            const auto pslocked = peer_ptr->state.lock();
                            return pslocked->state == disconnecting || pslocked->shut_write;
                        } ();
                    }
                }

                bool has_data;
                {
                    auto locked = receive.lock();
                    has_data = (type == sock_stream)
                        ? locked->buffered > 0
                        : !locked->dgram_queue.empty();
                }

                if (is_listener)
                {
                    auto slocked = state.lock();
                    if (!slocked->accept_queue.empty())
                        mask |= pollin | pollrdnorm;
                }
                else
                {
                    if (has_data || peer_gone)
                        mask |= pollin | pollrdnorm;

                    bool shut_write;
                    {
                        const auto slocked = state.lock();
                        shut_write = slocked->shut_write;
                    }

                    if (peer_gone)
                        mask |= pollrdhup;

                    if (type == sock_stream)
                    {
                        if (is_connected && peer_ptr && !shut_write && !peer_gone)
                        {
                            const auto prlocked = peer_ptr->receive.lock();
                            if ((prlocked->capacity - prlocked->buffered) > 0)
                                mask |= pollout | pollwrnorm;
                        }
                    }
                    else // sock_dgram and sock_seqpacket
                    {
                        if (!shut_write)
                        {
                            if (peer_ptr && !peer_gone)
                            {
                                const auto prlocked = peer_ptr->receive.lock();
                                std::size_t size = 0;
                                for (auto &dgram : prlocked->dgram_queue)
                                    size += dgram.payload.size();
                                if (size < dgram_cap)
                                    mask |= pollout | pollwrnorm;
                            }
                            else if (!is_connected)
                                mask |= pollout | pollwrnorm;
                        }
                    }

                    if (peer_gone)
                        mask |= pollhup;
                }

                return mask;
            }

            auto shutdown(int how) -> lib::expect<void> override
            {
                std::weak_ptr<unix_sock> peer_weak;
                bool was_listening = false;
                {
                    auto slocked = state.lock();
                    if (slocked->state != connected && slocked->state != listening)
                        return std::unexpected { lib::err::not_connected };

                    peer_weak = slocked->peer;
                    was_listening = (slocked->state == listening);

                    if (how == shut_rd || how == shut_rdwr)
                        slocked->shut_read = true;
                    if (how == shut_wr || how == shut_rdwr)
                        slocked->shut_write = true;
                }

                read_wait.wake_all();
                write_wait.wake_all();
                if (was_listening)
                {
                    accept_wait.wake_all();
                    conn_wait.wake_all();
                }

                if (auto peer_ptr = peer_weak.lock())
                {
                    peer_ptr->read_wait.wake_all();
                    peer_ptr->write_wait.wake_all();
                }

                return { };
            }

            auto getsockname(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> override
            {
                sockaddr_un sa { };
                sa.sun_family = af_unix;
                socklen_t actual = sizeof(addr_fam);

                {
                    auto slocked = state.lock();
                    if (!slocked->bound_path.empty())
                    {
                        const bool is_abstract = slocked->bound_path[0] == 0;
                        const auto copy_size = std::min(
                            slocked->bound_path.size(),
                            sizeof(sa.sun_path)
                        );
                        std::memcpy(sa.sun_path, slocked->bound_path.data(), copy_size);

                        actual = static_cast<socklen_t>(sizeof(addr_fam) +
                            slocked->bound_path.size() + !is_abstract);
                    }
                }

                const auto copy_len = std::min<std::size_t>(out.size(), actual);
                if (!out.subspan(0, copy_len)
                    .copy_from(std::as_bytes(std::span { &sa, 1 }).subspan(0, copy_len)))
                    return std::unexpected { lib::err::invalid_address };
                return actual;
            }

            auto getpeername(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> override
            {
                auto peer_ptr = state.lock()->peer.lock();
                if (!peer_ptr)
                    return std::unexpected { lib::err::not_connected };
                return peer_ptr->getsockname(out);
            }

            auto setsockopt(
                sock_lvl lvl, int opt,
                lib::maybe_uspan<const std::byte> buf
            ) -> lib::expect<void> override
            {
                if (lvl != sol_socket)
                    return std::unexpected { lib::err::protocol_unsupported };

                auto slocked = state.lock();
                const auto read_int = [&](int &out) -> lib::expect<void> {
                    if (buf.size() < sizeof(int))
                        return std::unexpected { lib::err::invalid_argument };

                    if (!buf.copy_to(std::as_writable_bytes(std::span { &out, 1 })))
                        return std::unexpected { lib::err::invalid_address };
                    return { };
                };

                switch (static_cast<sock_opt>(opt))
                {
                    case so_rcvbuf:
                    {
                        int val;
                        if (const auto ret = read_int(val); !ret)
                            return ret;

                        auto locked = receive.lock();
                        resize_storage(locked, val);
                        return { };
                    }
                    case so_sndbuf:
                    {
                        int val;
                        if (const auto ret = read_int(val); !ret)
                            return ret;

                        auto peer_ptr = slocked->peer.lock();
                        if (!peer_ptr)
                        {
                            slocked->resize_on_con = val;
                            return { };
                        }

                        auto locked = peer_ptr->receive.lock();
                        resize_storage(locked, val);
                        return { };
                    }
                    case so_passcred:
                    {
                        int val;
                        if (const auto ret = read_int(val); !ret)
                            return ret;

                        slocked->passcred = val != 0;
                        return { };
                    }
                    case so_linger:
                        if (buf.size() < sizeof(linger))
                            return std::unexpected { lib::err::invalid_argument };

                        if (!buf.copy_to(std::as_writable_bytes(std::span { &slocked->linger_opt, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return { };
                    case so_rcvtimeo:
                        if (buf.size() < sizeof(timeval))
                            return std::unexpected { lib::err::invalid_argument };

                        if (!buf.copy_to(std::as_writable_bytes(std::span { &slocked->rcvtimeo, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return { };
                    case so_sndtimeo:
                        if (buf.size() < sizeof(timeval))
                            return std::unexpected { lib::err::invalid_argument };

                        if (!buf.copy_to(std::as_writable_bytes(std::span { &slocked->sndtimeo, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return { };
                    case so_reuseaddr:
                    case so_reuseport:
                    case so_keepalive:
                    case so_broadcast:
                    case so_dontroute:
                        // no-op
                        return { };
                    default:
                        return std::unexpected { lib::err::invalid_argument };
                }
            }

            auto getsockopt(
                sock_lvl lvl, int opt,
                lib::maybe_uspan<std::byte> buf
            ) -> lib::expect<std::size_t> override
            {
                if (lvl != sol_socket)
                    return std::unexpected { lib::err::protocol_unsupported };

                auto slocked = state.lock();
                const auto copy = [&]<typename Type>(Type val) -> lib::expect<std::size_t> {
                    const auto num = std::min(buf.size(), sizeof(Type));
                    if (!buf.subspan(0, num)
                        .copy_from(std::as_bytes(std::span { &val, 1 }).subspan(0, num)))
                        return std::unexpected { lib::err::invalid_address };
                    return sizeof(Type);
                };

                switch (static_cast<sock_opt>(opt))
                {
                    case so_type:
                        return copy(static_cast<int>(type));
                    case so_domain:
                        return copy(static_cast<int>(family));
                    case so_protocol:
                        return copy(protocol);
                    case so_acceptconn:
                        return copy(slocked->state == listening ? 1 : 0);
                    case so_passcred:
                        return copy(slocked->passcred ? 1 : 0);
                    case so_error:
                    {
                        const int err = slocked->pending_error;
                        slocked->pending_error = 0;
                        return copy(err);
                    }
                    case so_rcvbuf:
                    {
                        const auto locked = receive.lock();
                        return copy(static_cast<int>(locked->capacity));
                    }
                    case so_sndbuf:
                        if (auto peer_ptr = slocked->peer.lock())
                        {
                            auto locked = peer_ptr->receive.lock();
                            return copy(static_cast<int>(locked->capacity));
                        }
                        return copy(static_cast<int>(default_capacity));
                    case so_peercred:
                    {
                        if (buf.size() < sizeof(ucred))
                            return std::unexpected { lib::err::invalid_argument };

                        auto peer_ptr = slocked->peer.lock();
                        if (!peer_ptr)
                            return std::unexpected { lib::err::not_connected };

                        ucred cred;
                        {
                            auto pslocked = peer_ptr->state.lock();
                            cred = pslocked->cred;
                        }

                        if (!buf.subspan(0, sizeof(ucred))
                            .copy_from(std::as_bytes(std::span { &cred, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return sizeof(ucred);
                    }
                    case so_linger:
                        if (buf.size() < sizeof(linger))
                            return std::unexpected { lib::err::invalid_argument };

                        if (!buf.subspan(0, sizeof(linger))
                            .copy_from(std::as_bytes(std::span { &slocked->linger_opt, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return sizeof(linger);
                    case so_rcvtimeo:
                        if (buf.size() < sizeof(timeval))
                            return std::unexpected { lib::err::invalid_argument };

                        if (!buf.subspan(0, sizeof(timeval))
                            .copy_from(std::as_bytes(std::span { &slocked->rcvtimeo, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return sizeof(timeval);
                    case so_sndtimeo:
                        if (buf.size() < sizeof(timeval))
                            return std::unexpected { lib::err::invalid_argument };

                        if (!buf.subspan(0, sizeof(timeval))
                            .copy_from(std::as_bytes(std::span { &slocked->sndtimeo, 1 })))
                            return std::unexpected { lib::err::invalid_address };
                        return sizeof(timeval);
                    default:
                        return std::unexpected { lib::err::invalid_argument };
                }
            }

            auto release() -> lib::expect<void> override
            {
                std::weak_ptr<unix_sock> peer_weak;
                std::vector<std::shared_ptr<unix_sock>> pending_connectors;
                lib::list<std::shared_ptr<unix_sock>> orphaned_queue;
                linger linger_opt;
                {
                    auto slocked = state.lock();
                    peer_weak = slocked->peer;
                    linger_opt = slocked->linger_opt;
                    if (slocked->bound && !slocked->bound_path.empty() &&
                        slocked->bound_path[0] == 0)
                    {
                        auto registry = abstract.lock();
                        registry->erase(slocked->bound_path);
                    }

                    slocked->state = disconnecting;
                    slocked->bound_inode.reset();
                    slocked->bound = false;

                    orphaned_queue = std::move(slocked->accept_queue);
                }

                pending_connectors.reserve(orphaned_queue.size());
                for (auto &client : orphaned_queue)
                {
                    auto cslocked = client->state.lock();
                    cslocked->state = disconnecting;
                    if (auto conn = cslocked->peer.lock())
                        pending_connectors.push_back(std::move(conn));
                }

                if (type == sock_stream && linger_opt.l_onoff && linger_opt.l_linger > 0)
                {
                    if (auto peer_ptr = peer_weak.lock())
                    {
                        const auto deadline = chrono::now(chrono::monotonic) +
                            timespec { linger_opt.l_linger, 0 };

                        while (true)
                        {
                            std::size_t gen;
                            {
                                auto plocked = peer_ptr->receive.lock();
                                if (plocked->buffered == 0)
                                    break;
                                gen = write_wait.snapshot_gen();
                            }

                            const auto now = chrono::now(chrono::monotonic);
                            if (now >= deadline)
                                break;

                            const auto remaining = (deadline - now).to_ns();
                            const auto res = write_wait.wait_prepared(gen, remaining);
                            if (res.interrupted || res.expired || res.killed)
                                break;
                        }
                    }
                }

                if (type == sock_stream && linger_opt.l_onoff && linger_opt.l_linger == 0)
                {
                    if (auto peer_ptr = peer_weak.lock())
                    {
                        auto plocked = peer_ptr->receive.lock();
                        plocked->buffered = 0;
                        plocked->head = 0;
                        plocked->tail = 0;
                        plocked->anc_queue.clear();
                    }
                }

                {
                    auto locked = receive.lock();
                    locked->anc_queue.clear();
                    locked->dgram_queue.clear();
                }

                read_wait.wake_all();
                write_wait.wake_all();
                accept_wait.wake_all();
                conn_wait.wake_all();

                if (auto peer_ptr = peer_weak.lock())
                {
                    peer_ptr->read_wait.wake_all();
                    peer_ptr->write_wait.wake_all();
                }

                for (auto &conn : pending_connectors)
                {
                    {
                        auto clocked = conn->state.lock();
                        if (clocked->state == connected || clocked->state == connecting)
                        {
                            clocked->state = disconnecting;
                            clocked->peer.reset();
                            clocked->pending_error = ECONNRESET;
                        }
                    }

                    conn->read_wait.wake_all();
                    conn->write_wait.wake_all();
                    conn->conn_wait.wake_all();
                }

                return { };
            }
        };

        lib::locker<
            lib::intrusive_list<
                unix_sock,
                &unix_sock::sockets_hook
            >, sched::mutex_t
        > sockets;

        void register_sock(unix_sock *sock)
        {
            sockets.lock()->push_back(sock);
        }

        void unregister_sock(unix_sock *sock)
        {
            sockets.lock()->remove(sock);
        }

        std::size_t copy_in(
            auto &locked, lib::maybe_uspan<std::byte> src,
            std::size_t off, std::size_t len, bool &fault
        )
        {
            fault = false;
            const auto first = std::min(len, locked->capacity - locked->head);
            const auto rest = len - first;

            if (!src.subspan(off, first).copy_to({ locked->storage.data() + locked->head, first }))
            {
                fault = true;
                return 0;
            }

            locked->head = (locked->head + first) & (locked->capacity - 1);
            locked->buffered += first;
            locked->total_produced += first;

            if (rest == 0)
                return first;

            if (!src.subspan(off + first, rest).copy_to({ locked->storage.data(), rest }))
            {
                fault = true;
                return first;
            }

            locked->head = (locked->head + rest) & (locked->capacity - 1);
            locked->buffered += rest;
            locked->total_produced += rest;

            return len;
        }

        std::size_t copy_out(
            auto &locked, lib::maybe_uspan<std::byte> dst,
            std::size_t off, std::size_t len, bool &fault
        )
        {
            fault = false;
            const auto first = std::min(len, locked->capacity - locked->tail);
            const auto rest = len - first;

            std::span span { locked->storage.data() + locked->tail, first };
            if (!dst.subspan(off, first).copy_from(span))
            {
                fault = true;
                return 0;
            }

            locked->tail = (locked->tail + first) & (locked->capacity - 1);
            locked->buffered -= first;
            locked->total_consumed += first;

            if (rest == 0)
                return first;

            if (!dst.subspan(off + first, rest).copy_from({ locked->storage.data(), rest }))
            {
                fault = true;
                return first;
            }

            locked->tail = (locked->tail + rest) & (locked->capacity - 1);
            locked->buffered -= rest;
            locked->total_consumed += rest;

            return len;
        }

        struct unix_family_t : family_t
        {
            unix_family_t() : family_t { af_unix } { }

            lib::expect<std::shared_ptr<socket_t>> create(sock_type type, int protocol) override
            {
                if (protocol != 0)
                    return std::unexpected { lib::err::protocol_unsupported };

                if (type != sock_stream && type != sock_dgram && type != sock_seqpacket)
                    return std::unexpected { lib::err::socket_unsupported };

                return std::make_shared<unix_sock>(protocol, type);
            }

            auto create_pair(sock_type type, int protocol)
                -> lib::expect<std::pair<std::shared_ptr<socket_t>, std::shared_ptr<socket_t>>> override
            {
                if (protocol != 0)
                    return std::unexpected { lib::err::protocol_unsupported };

                if (type != sock_stream && type != sock_dgram && type != sock_seqpacket)
                    return std::unexpected { lib::err::socket_unsupported };

                auto one = std::make_shared<unix_sock>(protocol, type, connected);
                auto two = std::make_shared<unix_sock>(protocol, type, connected);

                const auto proc = sched::current_process();
                const ucred cred {
                    .pid = proc->pid,
                    .uid = proc->cred->euid,
                    .gid = proc->cred->egid
                };

                {
                    auto locked = one->state.lock();
                    locked->peer = two;
                    locked->cred = cred;
                }
                {
                    auto locked = two->state.lock();
                    locked->peer = one;
                    locked->cred = cred;
                }

                return std::make_pair(std::move(one), std::move(two));
            }
        } unix_family;

        lib::initgraph::task unix_task
        {
            "socket.procfs.register-unix",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { registered_procfs_stage() },
            [] {
                if (const auto ret = register_family(&unix_family); !ret)
                {
                    lib::error(
                        "socket: could not register family 'unix': {}",
                        lib::error_name(ret.error())
                    );
                    return;
                }

                using namespace fs::procfs;
                lib::bug_on(!register_per_pid("net/unix",
                    make_file_ops([](auto) {
                        std::string out {
                            "Num       RefCount Protocol Flags    Type St Inode Path\n"
                        };

                        const auto locked = sockets.lock();
                        out.reserve(out.size() + locked->size() * 96);
                        auto it = std::back_inserter(out);
                        for (auto &sock : *locked)
                        {
                            const auto slocked = sock.state.lock();
                            const std::uint32_t flags = (slocked->state == listening) ? 0x10000 : 0;

                            std::uint32_t st = 0;
                            switch (slocked->state)
                            {
                                case unconnected:
                                    st = 0x01;
                                    break;
                                case connecting:
                                    st = 0x02;
                                    break;
                                case connected:
                                    st = 0x03;
                                    break;
                                case disconnecting:
                                    st = 0x04;
                                    break;
                                case listening:
                                    st = 0x0A;
                                    break;
                            }

                            const std::uint64_t inode = slocked->bound_inode
                                ? slocked->bound_inode->stat.st_ino : 0;

                            std::string path;
                            if (!slocked->bound_path.empty())
                            {
                                if (slocked->bound_path[0] == '\0')
                                {
                                    path = "@";
                                    path.append(
                                        slocked->bound_path.data() + 1,
                                        slocked->bound_path.size() - 1
                                    );
                                }
                                else path = slocked->bound_path;
                            }

                            fmt::format_to(it,
                                "{:016x}: {:08x} {:08x} {:08x} {:04x} {:02x} {:5}{}{}\n",
                                reinterpret_cast<std::uintptr_t>(&sock),
                                static_cast<std::uint32_t>(sock.weak_from_this().use_count()),
                                0, flags, static_cast<std::uint32_t>(sock.type),
                                st, inode, path.empty() ? "" : " ", path
                            );
                        }

                        return out;
                    }), node_type::file, 0444
                ));
            }
        };
    } // namespace
} // namespace vfs::socket
