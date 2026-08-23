// Copyright (C) 2024-2026  ilobilo

export module system.vfs.socket:inet;

import lib;
import std;

import :base;

export
{
    enum ip_proto
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

    enum ip_opt
    {
        ip_tos = 1,
        ip_ttl = 2,
        ip_hdrincl = 3,
        ip_options = 4,
        ip_router_alert = 5,
        ip_recvopts = 6,
        ip_retopts = 7,
        ip_pktinfo = 8,
        ip_pktoptions = 9,
        ip_mtu_discover = 10,
        ip_recverr = 11,
        ip_recvttl = 12,
        ip_recvtos = 13,
        ip_mtu = 14,
        ip_freebind = 15,
        ip_ipsec_policy = 16,
        ip_xfrm_policy = 17,
        ip_passsec = 18,
        ip_transparent = 19,
        ip_recvretopts = ip_retopts,
        ip_origdstaddr = 20,
        ip_recvorigdstaddr = ip_origdstaddr,
        ip_minttl = 21,
        ip_nodefrag = 22,
        ip_checksum = 23,
        ip_bind_address_no_port = 24,
        ip_recvfragsize = 25,
        ip_recverr_rfc4884 = 26,
        ip_multicast_if = 32,
        ip_multicast_ttl = 33,
        ip_multicast_loop = 34,
        ip_add_membership = 35,
        ip_drop_membership = 36,
        ip_unblock_source = 37,
        ip_block_source = 38,
        ip_add_source_membership = 39,
        ip_drop_source_membership = 40,
        ip_msfilter = 41,
        mcast_join_group = 42,
        mcast_block_source = 43,
        mcast_unblock_source = 44,
        mcast_leave_group = 45,
        mcast_join_source_group = 46,
        mcast_leave_source_group = 47,
        mcast_msfilter = 48,
        ip_multicast_all = 49,
        ip_unicast_if = 50,
        ip_local_port_range = 51,
        ip_protocol = 52
    };

    struct in_addr
    {
        lib::big_u32_t addr;
    };

    struct sockaddr_in
    {
        addr_fam family;
        lib::big_u16_t port;
        in_addr addr;
        unsigned char zero[16 - sizeof(std::uint16_t) * 2 - sizeof(in_addr)];
    };
    static_assert(sizeof(sockaddr_in) == 16);

    struct in6_addr
    {
        union {
            std::uint8_t addr8[16];
            lib::big_u16_t addr16[8];
            lib::big_u32_t addr32[4];
        };
    };

    struct sockaddr_in6
    {
        std::uint16_t family;
        lib::big_u16_t port;
        lib::big_u32_t flowinfo;
        in6_addr sin6_addr;
        std::uint32_t scope_id;
    };

    struct ifmap
    {
        std::uint64_t mem_start;
        std::uint64_t mem_end;
        std::uint16_t base_addr;
        std::uint8_t irq;
        std::uint8_t dma;
        std::uint8_t port;
    };

    struct if_settings
    {
        std::uint32_t type;
        std::uint32_t size;
        union {
            struct raw_hdlc_proto *raw_hdlc;
            struct cisco_proto *cisco;
            struct fr_proto *fr;
            struct fr_proto_pvc *fr_pvc;
            struct fr_proto_pvc_info *fr_pvc_info;
            struct x25_hdlc_proto *x25;

            struct sync_serial_settings *sync;
            struct te1_settings *te1;
        } ifsu;
    };

    constexpr std::size_t ifnamsiz = 16;
    struct ifreq
    {
        union {
            char name[ifnamsiz];
        };

        union {
            sockaddr_in addr;
            sockaddr_in dstaddr;
            sockaddr_in broadaddr;
            sockaddr_in netmask;
            sockaddr hwaddr;
            short flags;
            int ivalue;
            int mtu;
            ifmap map;
            char slave[ifnamsiz];
            char newname[ifnamsiz];
            void *data;
            if_settings settings;
        };
    };

    struct ifconf
    {
        int len;
        union {
            char __user *buf;
            ifreq __user *req;
        };
    };

    struct rtentry
    {
        std::uint64_t pad1;
        sockaddr_in dst;
        sockaddr_in gateway;
        sockaddr_in genmask;
        std::uint16_t flags;
        std::int16_t pad2;
        std::uint64_t   pad3;
        void *pad4;
        std::int16_t metric;
        char __user *dev;
        std::uint64_t mtu;
        std::uint64_t window;
        std::uint16_t irtt;
    };
} // export
