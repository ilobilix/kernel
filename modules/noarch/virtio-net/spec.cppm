// Copyright (C) 2024-2026  ilobilo

export module vnet:spec;

import magic_enum;
import std;

export namespace vnet
{
    constexpr std::uint16_t mq_pairs_min = 1;
    constexpr std::uint16_t mq_pairs_max = 0x8000;

    enum class feature : std::uint64_t
    {
        csum = (1ul << 0),
        guest_csum = (1ul << 1),
        ctrl_guest_offloads = (1ul << 2),
        mtu = (1ul << 3),
        mac = (1ul << 5),
        guest_tso4 = (1ul << 7),
        guest_tso6 = (1ul << 8),
        guest_ecn = (1ul << 9),
        guest_ufo = (1ul << 10),
        host_tso4 = (1ul << 11),
        host_tso6 = (1ul << 12),
        host_ecn = (1ul << 13),
        host_ufo = (1ul << 14),
        mrg_rxbuf = (1ul << 15),
        status = (1ul << 16),
        ctrl_vq = (1ul << 17),
        ctrl_rx = (1ul << 18),
        ctrl_vlan = (1ul << 19),
        ctrl_rx_extra = (1ul << 20),
        guest_announce = (1ul << 21),
        mq = (1ul << 22),
        ctrl_mac_addr = (1ul << 23),
        hash_tunnel = (1ul << 51),
        vq_notf_coal = (1ul << 52),
        notf_coal = (1ul << 53),
        guest_uso4 = (1ul << 54),
        guest_uso6 = (1ul << 55),
        host_uso = (1ul << 56),
        hash_report = (1ul << 57),
        guest_hdrlen = (1ul << 59),
        rss = (1ul << 60),
        rsc_ext = (1ul << 61),
        standby = (1ul << 62),
        speed_duplex = (1ul << 63)
    };
    using namespace magic_enum::bitwise_operators;

    enum class status : std::uint16_t
    {
        none = 0,
        link_up = 1,
        announce = 2
    };

    struct [[gnu::packed]] net_config_t
    {
        std::uint8_t mac[6];
        status status;
        std::uint16_t max_virtqueue_pairs;
        std::uint16_t mtu;
        std::uint32_t speed;
        std::uint8_t duplex;
        std::uint8_t rss_max_key_size;
        std::uint16_t rss_max_indirection_table_length;
        std::uint32_t supported_hash_types;
        std::uint32_t supported_tunnel_types;
    };

    enum class flag : std::uint8_t
    {
        needs_csum = 1,
        data_valid = 2,
        rsc_info = 4
    };

    enum class gso : std::uint8_t
    {
        none = 0,
        tcpv4 = 1,
        udp = 3,
        tcpv6 = 4,
        udp_l4 = 5,
        ecn = 0x80
    };

    struct net_hdr_t
    {
        flag flags;
        gso gso_type;
        std::uint16_t hdr_len;
        std::uint16_t gso_size;
        std::uint16_t csum_start;
        std::uint16_t csum_offset;
        // with mrg_rxbuf or version_1
        // std::uint16_t num_buffers;
        // // with hash_report
        // std::uint32_t hash_value;
        // std::uint16_t hash_report;
        // std::uint16_t padding_reserved;
    };
    static_assert(sizeof(net_hdr_t) == 10);

    enum class ctrl_ack : std::uint8_t
    {
        ok = 0,
        err = 1
    };

    enum class ctrl_class : std::uint8_t
    {
        rx = 0,
        mac = 1,
        vlan = 2,
        announce = 3,
        mq = 4,
        guest_offloads = 5,
        notf_coal = 6
    };

    enum class ctrl_mac : std::uint8_t
    {
        table_set = 0,
        addr_set = 1
    };

    enum class ctrl_mq : std::uint8_t
    {
        vq_pairs_set = 0,
        rss_config = 1,
        hash_config = 2
    };

    struct net_ctrl_t
    {
        ctrl_class class_;
        std::uint8_t command;
        std::uint8_t data[];
        // ctrl_ack ack;
    };
} // export namespace vnet
