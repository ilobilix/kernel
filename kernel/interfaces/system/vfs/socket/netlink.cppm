// Copyright (C) 2024-2026  ilobilo

export module system.vfs.socket:netlink;

import lib;
import std;

import :base;

export
{
    enum netlink_proto
    {
        netlink_route = 0,           // Routing/device hook
        netlink_unused = 1,          // Unused number
        netlink_usersock = 2,        // Reserved for user mode socket protocols
        netlink_firewall = 3,        // Unused number, formerly ip_queue
        netlink_sock_diag = 4,       // socket monitoring
        netlink_nflog = 5,           // netfilter/iptables ULOG
        netlink_xfrm = 6,            // ipsec
        netlink_selinux = 7,         // SELinux event notifications
        netlink_iscsi = 8,           // Open-iSCSI
        netlink_audit = 9,           // auditing
        netlink_fib_lookup = 10,
        netlink_connector = 11,
        netlink_netfilter = 12,      // netfilter subsystem
        netlink_ip6_fw = 13,
        netlink_dnrtmsg = 14,        // DECnet routing messages (obsolete)
        netlink_kobject_uevent = 15, // Kernel messages to userspace
        netlink_generic = 16,
        netlink_scsitransport = 18,  // SCSI Transports
        netlink_ecryptfs = 19,
        netlink_rdma = 20,
        netlink_crypto = 21,         // Crypto layer
        netlink_smc = 22,            // SMC monitoring
        netlink_max_links = 32,
        netlink_inet_diag = netlink_sock_diag
    };

    enum nlm_flag : std::uint16_t
    {
        nlm_f_request = 0x01,       // It is request message.
        nlm_f_multi = 0x02,         // Multipart message, terminated by nlmsg_done
        nlm_f_ack = 0x04,           // Reply with ack, with zero or error code
        nlm_f_echo = 0x08,          // Receive resulting notifications
        nlm_f_dump_intr = 0x10,     // Dump was inconsistent due to sequence change
        nlm_f_dump_filtered = 0x20, // Dump was filtered as requested

        // Modifiers to GET request
        nlm_f_root = 0x100,         // Specify tree root
        nlm_f_match = 0x200,        // Return all matching
        nlm_f_atomic = 0x400,       // Atomic get
        nlm_f_dump  = (nlm_f_root | nlm_f_match),

        // Modifiers to NEW request
        nlm_f_replace = 0x100,      // Override existing
        nlm_f_excl = 0x200,         // Do not touch, if it exists
        nlm_f_create = 0x400,       // Create, if it does not exist
        nlm_f_append = 0x800,       // Add to end of list

        // Modifiers to DELETE request
        nlm_f_nonrec = 0x100,       // Do not delete recursively
        nlm_f_bulk = 0x200,         // Delete multiple objects

        // Flags for ACK message
        nlm_f_capped = 0x100,       // Request was capped
        nlm_f_ack_tlvs = 0x200      // Extended ack tvls were included
    };

    enum nlmsg_type : std::uint16_t
    {
        nlmsg_noop = 0x1,
        nlmsg_error = 0x2,
        nlmsg_done = 0x3,
        nlmsg_overrun = 0x4,
        nlmsg_min_type = 0x10
    };

    enum netlink_opt
    {
        netlink_add_membership = 1,
        netlink_drop_membership = 2,
        netlink_pktinfo = 3,
        netlink_broadcast_error = 4,
        netlink_no_enobufs = 5,
        netlink_rx_ring = 6,
        netlink_tx_ring = 7,
        netlink_listen_all_nsid = 8,
        netlink_list_memberships = 9,
        netlink_cap_ack = 10,
        netlink_ext_ack = 11,
        netlink_get_strict_chk = 12
    };

    struct sockaddr_nl
    {
        addr_fam family;
        std::uint16_t pad;
        std::uint32_t pid;
        std::uint32_t groups;
    };

    struct nlmsghdr
    {
        std::uint32_t len;
        std::uint16_t type;
        std::uint16_t flags;
        std::uint32_t seq;
        std::uint32_t pid;
    };
} // export

export namespace vfs::socket::netlink
{
    constexpr std::uint32_t group_max = 32;

    using reply_t = std::function<lib::expect<void> (std::span<const std::byte>)>;
    struct proto_t
    {
        int protocol;
        std::uint32_t ngroups;
        bool nonroot_recv;
        bool nonroot_send;

        proto_t(int protocol, std::uint32_t ngroups, bool nonroot_recv, bool nonroot_send)
            : protocol { protocol }, ngroups { ngroups },
              nonroot_recv { nonroot_recv }, nonroot_send { nonroot_send } { }

        virtual void input(
            std::uint32_t src_portid, std::span<const std::byte> msg,
            const reply_t &reply
        )
        { lib::unused(src_portid, msg, reply); }
    };

    lib::expect<void> register_proto(proto_t &proto);

    lib::expect<void> broadcast(
        int protocol, std::uint32_t group,
        std::span<const std::byte> payload, std::uint32_t src_portid = 0
    );

    lib::expect<void> unicast(
        int protocol, std::uint32_t portid,
        std::span<const std::byte> payload, std::uint32_t src_portid = 0
    );
} // export namespace vfs::socket::netlink
