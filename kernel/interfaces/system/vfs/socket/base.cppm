// Copyright (C) 2024-2026  ilobilo

export module system.vfs.socket:base;

import system.vfs;
import lib;
import std;

export
{
    constexpr int somaxconn = 4096;
    constexpr std::size_t uio_maxiov = 1024;

    enum prot_fam : int
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

    enum sock_lvl : int
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

    enum sock_opt : int
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
        so_acceptconn = 30,
        so_protocol = 38,
        so_domain = 39
    };

    enum ip_proto : int
    {
        ipproto_ip = 0,
        ipproto_icmp = 1,
        ipproto_igmp = 2,
        ipproto_ipip = 4,
        ipproto_tcp = 6,
        ipproto_egp = 8,
        ipproto_pup = 12,
        ipproto_udp = 17,
        ipproto_idp = 22,
        ipproto_tp = 29,
        ipproto_dccp = 33,
        ipproto_ipv6 = 41,
        ipproto_rsvp = 46,
        ipproto_gre = 47,
        ipproto_esp = 50,
        ipproto_ah = 51,
        ipproto_mtp = 92,
        ipproto_beetph = 94,
        ipproto_encap = 98,
        ipproto_pim = 103,
        ipproto_comp = 108,
        ipproto_l2tp = 115,
        ipproto_sctp = 132,
        ipproto_udplite = 136,
        ipproto_mpls = 137,
        ipproto_ethernet = 143,
        ipproto_aggfrag = 144,
        ipproto_raw = 255,
        ipproto_smc = 256,
        ipproto_mptcp = 262,
        ipproto_max
    };

    enum sock_type : int
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

    enum scm_type : int
    {
        scm_rights = 1,
        scm_credentials = 2
    };

    enum msg_flag : int
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

    enum shut : int
    {
        shut_rd = 0,
        shut_wr = 1,
        shut_rdwr = 2,
    };

    struct sockaddr
    {
        addr_fam sa_family;
        char sa_data[14];
    };

    struct alignas(std::uint64_t) sockaddr_storage
    {
        addr_fam ss_family;
        char ss_padding[126];
    };

    struct msghdr
    {
        void __user *msg_name;
        socklen_t msg_namelen;
        iovec __user *msg_iov;
        std::size_t msg_iovlen;
        void __user *msg_control;
        std::size_t msg_controllen;
        int msg_flags;
    };

    struct cmsghdr
    {
        std::size_t cmsg_len;
        int cmsg_level;
        int cmsg_type;
    };

    struct linger
    {
        int l_onoff;
        int l_linger;
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
    };

    struct socket_t
    {
        int protocol;
        addr_fam family;
        sock_type type;

        socket_t(int protocol, addr_fam family, sock_type type)
            : protocol { protocol }, family { family }, type { type } { }

        virtual auto bind(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void> = 0;
        virtual auto connect(lib::maybe_uspan<const std::byte> addr) -> lib::expect<void> = 0;
        virtual auto listen(int backlog) -> lib::expect<void> = 0;
        virtual auto accept(
            lib::maybe_uspan<std::byte> peer_addr_out,
            socklen_t *addr_len_inout, bool nonblock
        ) -> lib::expect<std::shared_ptr<socket_t>> = 0;

        virtual auto sendmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> = 0;
        virtual auto recvmsg(msg_header_t &hdr, int flags) -> lib::expect<std::size_t> = 0;

        virtual auto ioctl(std::uint64_t request, lib::uptr_or_addr argp) -> lib::expect<int> = 0;
        virtual auto poll(vfs::poll_table *pt) -> lib::expect<std::uint16_t> = 0;

        virtual auto shutdown(int how) -> lib::expect<void> = 0;

        virtual auto getsockname(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> = 0;
        virtual auto getpeername(lib::maybe_uspan<std::byte> out) -> lib::expect<socklen_t> = 0;

        virtual auto setsockopt(
            sock_lvl lvl, int opt,
            lib::maybe_uspan<const std::byte> buf
        ) -> lib::expect<void> = 0;
        virtual auto getsockopt(
            sock_lvl lvl, int opt,
            lib::maybe_uspan<std::byte> buf
        ) -> lib::expect<std::size_t> = 0;

        virtual auto release() -> lib::expect<void> = 0;

        virtual ~socket_t() = default;
    };

    std::shared_ptr<socket_t> from_file(const vfs::file &file);

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

    lib::expect<void> register_family(family_t *fam);

    auto create(addr_fam af, sock_type type, int protocol)
        -> lib::expect<std::shared_ptr<socket_t>>;
    auto create_pair(addr_fam af, sock_type type, int protocol)
        -> lib::expect<std::pair<std::shared_ptr<socket_t>, std::shared_ptr<socket_t>>>;
    auto create_anon(std::shared_ptr<socket_t> sock, int flags) -> lib::expect<int>;

    std::shared_ptr<vfs::ops> get_ops();
} // export namespace vfs::socket
