// Copyright (C) 2024-2026  ilobilo

export module system.vfs.socket:base;

import system.sched.wait_queue;
import system.sched.mutex;
import system.vfs;
import lib;
import std;

export
{
    constexpr int somaxconn = 4096;
    constexpr std::size_t uio_maxiov = 1024;
    constexpr uid_t unset_id = -1u;

    constexpr std::size_t cmsg_align = sizeof(std::size_t);

    enum prot_fam
    {
        pf_unspec = 0,
        pf_local = 1,
        pf_unix = pf_local,
        pf_file = pf_local,
        pf_inet = 2,
        pf_ax25 = 3,
        pf_ipx = 4,
        pf_appletalk = 5,
        pf_netrom = 6,
        pf_bridge = 7,
        pf_atmpvc = 8,
        pf_x25 = 9,
        pf_inet6 = 10,
        pf_rose = 11,
        pf_decnet = 12,
        pf_netbeui = 13,
        pf_security = 14,
        pf_key = 15,
        pf_netlink = 16,
        pf_route = pf_netlink,
        pf_packet = 17,
        pf_ash = 18,
        pf_econet = 19,
        pf_atmsvc = 20,
        pf_rds = 21,
        pf_sna = 22,
        pf_irda = 23,
        pf_pppox = 24,
        pf_wanpipe = 25,
        pf_llc = 26,
        pf_ib = 27,
        pf_mpls = 28,
        pf_can = 29,
        pf_tipc = 30,
        pf_bluetooth = 31,
        pf_iucv = 32,
        pf_rxrpc = 33,
        pf_isdn = 34,
        pf_phonet = 35,
        pf_ieee802154 = 36,
        pf_caif = 37,
        pf_alg = 38,
        pf_nfc = 39,
        pf_vsock = 40,
        pf_kcm = 41,
        pf_qipcrtr = 42,
        pf_smc = 43,
        pf_xdp = 44,
        pf_mctp = 45,
        pf_max = 46
    };

    enum addr_fam : std::uint16_t
    {
        af_unspec = pf_unspec,
        af_local = pf_local,
        af_unix = pf_unix,
        af_file = pf_file,
        af_inet = pf_inet,
        af_ax25 = pf_ax25,
        af_ipx = pf_ipx,
        af_appletalk = pf_appletalk,
        af_netrom = pf_netrom,
        af_bridge = pf_bridge,
        af_atmpvc = pf_atmpvc,
        af_x25 = pf_x25,
        af_inet6 = pf_inet6,
        af_rose = pf_rose,
        af_decnet = pf_decnet,
        af_netbeui = pf_netbeui,
        af_security = pf_security,
        af_key = pf_key,
        af_netlink = pf_netlink,
        af_route = pf_route,
        af_packet = pf_packet,
        af_ash = pf_ash,
        af_econet = pf_econet,
        af_atmsvc = pf_atmsvc,
        af_rds = pf_rds,
        af_sna = pf_sna,
        af_irda = pf_irda,
        af_pppox = pf_pppox,
        af_wanpipe = pf_wanpipe,
        af_llc = pf_llc,
        af_ib = pf_ib,
        af_mpls = pf_mpls,
        af_can = pf_can,
        af_tipc = pf_tipc,
        af_bluetooth = pf_bluetooth,
        af_iucv = pf_iucv,
        af_rxrpc = pf_rxrpc,
        af_isdn = pf_isdn,
        af_phonet = pf_phonet,
        af_ieee802154 = pf_ieee802154,
        af_caif = pf_caif,
        af_alg = pf_alg,
        af_nfc = pf_nfc,
        af_vsock = pf_vsock,
        af_kcm = pf_kcm,
        af_qipcrtr = pf_qipcrtr,
        af_smc = pf_smc,
        af_xdp = pf_xdp,
        af_mctp = pf_mctp,
        af_max = pf_max
    };

    enum sock_lvl
    {
        sol_socket = 1,
        sol_raw = 255,
        sol_decnet = 261,
        sol_x25 = 262,
        sol_packet = 263,
        sol_atm = 264,
        sol_aal = 265,
        sol_irda = 266,
        sol_netbeui = 267,
        sol_llc = 268,
        sol_dccp = 269,
        sol_netlink = 270,
        sol_tipc = 271,
        sol_rxrpc = 272,
        sol_pppol2tp = 273,
        sol_bluetooth = 274,
        sol_pnpipe = 275,
        sol_rds = 276,
        sol_iucv = 277,
        sol_caif = 278,
        sol_alg = 279,
        sol_nfc = 280,
        sol_kcm = 281,
        sol_tls = 282,
        sol_xdp = 283,
        sol_mptcp = 284,
        sol_mctp = 285,
        sol_smc = 286,
        sol_vsock = 287
    };

    enum sock_opt
    {
        so_debug = 1,
        so_reuseaddr = 2,
        so_type = 3,
        so_error = 4,
        so_dontroute = 5,
        so_broadcast = 6,
        so_sndbuf = 7,
        so_rcvbuf = 8,
        so_keepalive = 9,
        so_oobinline = 10,
        so_linger = 13,
        so_reuseport = 15,
        so_passcred = 16,
        so_peercred = 17,
        so_rcvtimeo = 20,
        so_sndtimeo = 21,
        so_attach_filter = 26,
        so_detach_filter = 27,
        so_acceptconn = 30,
        so_sndbufforce = 32,
        so_rcvbufforce = 33,
        so_protocol = 38,
        so_domain = 39,
        so_lock_filter = 44
    };

    enum sock_type
    {
        sock_stream = 1,
        sock_dgram = 2,
        sock_raw = 3,
        sock_rdm = 4,
        sock_seqpacket = 5,
        sock_dccp = 6,
        sock_packet = 10
    };

    enum sock_flags : std::uint32_t
    {
        sock_cloexec = 02000000,
        sock_nonblock = 00004000
    };

    enum scm_type
    {
        scm_rights = 1,
        scm_credentials = 2
    };

    enum msg_flag
    {
        msg_oob = 0x01,
        msg_peek = 0x02,
        msg_dontroute = 0x04,
        msg_ctrunc = 0x08,
        msg_proxy = 0x10,
        msg_trunc = 0x20,
        msg_dontwait = 0x40,
        msg_eor = 0x80,
        msg_waitall = 0x0100,
        msg_fin = 0x200,
        msg_syn = 0x400,
        msg_confirm = 0x800,
        msg_rst = 0x1000,
        msg_errqueue = 0x2000,
        msg_nosignal = 0x4000,
        msg_more = 0x8000,
        msg_waitforone = 0x10000,
        msg_batch = 0x40000,
        msg_sock_devmem = 0x2000000,
        msg_zerocopy = 0x4000000,
        msg_fastopen = 0x20000000,
        msg_cmsg_cloexec = 0x40000000
    };

    enum shut
    {
        shut_rd = 0,
        shut_wr = 1,
        shut_rdwr = 2,
    };

    struct sockaddr
    {
        addr_fam family;
        char data[14];
    };

    struct alignas(std::uint64_t) sockaddr_storage
    {
        addr_fam family;
        char padding[126];
    };

    struct msghdr
    {
        void __user *name;
        socklen_t namelen;
        iovec __user *iov;
        std::size_t iovlen;
        void __user *control;
        std::size_t controllen;
        int flags;
    };

    struct mmsghdr
    {
        msghdr hdr;
        std::uint32_t len;
    };

    struct cmsghdr
    {
        std::size_t len;
        int level;
        int type;
    };

    struct linger
    {
        int onoff;
        int linger;
    };

    struct ucred
    {
        pid_t pid;
        uid_t uid;
        gid_t gid;
    };
} // export

export namespace vfs::socket
{
    struct msg_header_t
    {
        lib::maybe_uspan<std::byte> name;
        std::span<lib::maybe_uspan<std::byte>> iovs;
        lib::maybe_uspan<std::byte> msgctrl;
        socklen_t msgctrl_len_out;
        socklen_t addr_len_out;
        int out_flags;

        lib::expect<bool> write_cmsg(int type, std::span<const std::byte> data);
    };

    enum socket_state
    {
        unconnected,
        connecting,
        connected,
        listening,
        disconnecting
    };

    struct core_state_t
    {
        socket_state state = unconnected;

        std::size_t sndbuf = 0;
        std::size_t rcvbuf = 0;

        bool shut_read = false;
        bool shut_write = false;

        bool passcred = false;
        bool reuseaddr = false;
        bool reuseport = false;
        bool broadcast = false;
        bool keepalive = false;
        bool dontroute = false;

        timeval rcvtimeo { }, sndtimeo { };
        linger linger_opt { };

        std::optional<ucred> peer_cred;
    };

    using core_state_ptr = lib::locked_ptr<core_state_t, sched::mutex_t>;

    struct socket_t
    {
        const int protocol;
        const addr_fam family;
        const sock_type type;

        core_state_ptr core_state;

        std::atomic<errnos> pending_err { };

        sched::wait_queue_t read_wait { };
        sched::wait_queue_t write_wait { };
        sched::wait_queue_t accept_wait { };

        socket_t(int protocol, addr_fam family, sock_type type, core_state_ptr core_state)
            : protocol { protocol }, family { family }, type { type },
              core_state { std::move(core_state) } { }

        virtual auto bind(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void> = 0;
        virtual auto connect(
            lib::maybe_uspan<const std::byte> addr, bool nonblock
        ) -> lib::expect<void> = 0;
        virtual auto listen(int backlog) -> lib::expect<void> = 0;
        virtual auto accept(
            lib::maybe_uspan<std::byte> peer_addr_out,
            socklen_t *addr_len_inout, bool nonblock
        ) -> lib::expect<std::shared_ptr<socket_t>> = 0;

        virtual auto sendmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> = 0;
        virtual auto recvmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> = 0;

        virtual auto ioctl(std::uint64_t request, lib::uptr_or_addr argp) -> lib::expect<int> = 0;
        virtual auto poll(vfs::poll_table_t *pt) -> lib::expect<std::uint16_t> = 0;

        virtual auto shutdown(int how) -> lib::expect<void> = 0;

        virtual auto getsockname(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> = 0;
        virtual auto getpeername(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> = 0;

        virtual auto do_setsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<const std::byte> buf)
            -> lib::expect<void> = 0;
        virtual void sol_sock_finish(core_state_t &core, sock_opt opt) = 0;
        virtual auto do_getsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<std::byte> buf)
            -> lib::expect<std::size_t> = 0;

        auto setsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<const std::byte> buf)
            -> lib::expect<void>;
        auto getsockopt(sock_lvl lvl, int opt, lib::maybe_uspan<std::byte> buf)
            -> lib::expect<std::size_t>;

        virtual auto release() -> lib::expect<void> = 0;

        virtual ~socket_t() = default;
    };

    template<typename Type>
    struct socket_impl_t : socket_t
    {
        struct state_t : core_state_t, Type { };
        using state_ptr = lib::locked_ptr<state_t, sched::mutex_t>;

        state_ptr state;

        socket_impl_t(int protocol, addr_fam family, sock_type type)
            : socket_impl_t {
                protocol, family, type,
                lib::make_locked<state_t, sched::mutex_t>()
            } { }

        private:
        socket_impl_t(int protocol, addr_fam family, sock_type type, state_ptr state)
            : socket_t { protocol, family, type, state }, state { std::move(state) } { }
    };

    std::shared_ptr<socket_t> from_file(const vfs::file_t &file);

    struct family_t
    {
        addr_fam af;

        family_t(addr_fam af) : af { af } { }

        virtual lib::expect<std::shared_ptr<socket_t>> create(sock_type type, int protocol) = 0;
        virtual auto create_pair(sock_type type, int protocol)
            -> lib::expect<std::pair<std::shared_ptr<socket_t>, std::shared_ptr<socket_t>>>
        {
            lib::unused(type, protocol);
            return std::unexpected { lib::err::not_supported };
        }
    };

    std::shared_ptr<vfs::ops_t> sock_ops();

    lib::expect<void> register_family(family_t *fam);

    auto create(addr_fam af, sock_type type, int protocol)
        -> lib::expect<std::shared_ptr<socket_t>>;
    auto create_pair(addr_fam af, sock_type type, int protocol)
        -> lib::expect<std::pair<std::shared_ptr<socket_t>, std::shared_ptr<socket_t>>>;
    auto create_anon(std::shared_ptr<socket_t> sock, int flags) -> lib::expect<int>;
} // export namespace vfs::socket
