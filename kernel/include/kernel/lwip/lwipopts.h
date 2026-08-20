// Copyright (C) 2024-2026  ilobilo

#pragma once

#define NO_SYS 0

#define LWIP_SOCKET    0
#define LWIP_NETCONN   1
#define LWIP_NETIF_API 1
#define LWIP_RAW       1
#define LWIP_TCP       1
#define LWIP_UDP       1

#define LWIP_IPV4           1
#define LWIP_IPV6           1
#define LWIP_DHCP           0
#define LWIP_AUTOIP         0
#define LWIP_DNS            1
#define LWIP_ICMP           1
#define LWIP_IGMP           1
#define LWIP_ARP            1
#define LWIP_ETHERNET       1
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_NETIF_LOOPBACK_MULTITHREADING 1

#define LWIP_HAVE_LOOPIF     0
#define IPV6_FRAG_COPYHEADER 1

#define MEM_ALIGNMENT   8
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define MEM_USE_POOLS   0

#define LWIP_SO_RCVBUF 1
#define SO_REUSE 1

#define LWIP_PROVIDE_ERRNO

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((format(printf, 1, 2)))
void lwip_port_diag(const char *fmt, ...);
__attribute__((noreturn))
void lwip_port_assert(const char *msg, const char *file, int line);

#ifdef __cplusplus
} // extern "C"
#endif

#define LWIP_PLATFORM_DIAG(x)   do { lwip_port_diag x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) lwip_port_assert((x), __FILE__, __LINE__)

#define LWIP_STATS 0
#define LWIP_DEBUG 0

// TCP
// #define TCP_MSS           460
// #define TCP_WND           (8 * TCP_MSS)
// #define TCP_SND_BUF       (8 * TCP_MSS)
// #define TCP_SND_QUEUELEN  ((4 * TCP_SND_BUF) / TCP_MSS)
// #define LWIP_WND_SCALE    1
// #define TCP_RCV_SCALE     2
// #define LWIP_TCP_SACK_OUT 1

// memory pool sizing
// #define MEM_SIZE                (64 * 1024)
// #define MEMP_NUM_PBUF           32
// #define MEMP_NUM_TCP_PCB        16
// #define MEMP_NUM_TCP_PCB_LISTEN 8
// #define MEMP_NUM_UDP_PCB        8
// #define MEMP_NUM_NETCONN        16
// #define PBUF_POOL_SIZE          32
// #define PBUF_POOL_BUFSIZE       1536

// threading sizes
// #define TCPIP_THREAD_STACKSIZE    0x4000
// #define TCPIP_THREAD_PRIO         0
#define TCPIP_MBOX_SIZE           128
#define DEFAULT_UDP_RECVMBOX_SIZE 64
#define DEFAULT_TCP_RECVMBOX_SIZE 64
#define DEFAULT_RAW_RECVMBOX_SIZE 32
#define DEFAULT_ACCEPTMBOX_SIZE   32

#define LWIP_TCPIP_CORE_LOCKING 1

// checksum offload (0 means nic handles it)
// #define CHECKSUM_GEN_IP    1
// #define CHECKSUM_GEN_TCP   1
// #define CHECKSUM_GEN_UDP   1
// #define CHECKSUM_CHECK_IP  1
// #define CHECKSUM_CHECK_TCP 1
// #define CHECKSUM_CHECK_UDP 1

// debugging
// #undef LWIP_DEBUG
// #define LWIP_DEBUG   1
// #define TCP_DEBUG    LWIP_DBG_ON
// #define DHCP_DEBUG   LWIP_DBG_ON
// #define DNS_DEBUG    LWIP_DBG_ON
// #define ETHARP_DEBUG LWIP_DBG_ON
// #define NETIF_DEBUG  LWIP_DBG_ON
// #define PBUF_DEBUG   LWIP_DBG_ON

// stats
// #undef LWIP_STATS
// #define LWIP_STATS 1
// #define MEM_STATS  1
// #define MEMP_STATS 1
// #define LINK_STATS 1
// #define IP_STATS   1
// #define TCP_STATS  1
// #define UDP_STATS  1

// apps
// #define LWIP_HTTPD
// #define LWIP_HTTPD_CLIENT
// #define LWIP_SNTP
// #define LWIP_MDNS_RESPONDER
// #define LWIP_MQTT
// #define LWIP_TFTP
// #define LWIP_SMTP_CLIENT
// #define LWIP_SNMP
// #define LWIP_LWIPERF
// #define LWIP_NETBIOS_RESPOND
// #define LWIP_ALTCP_TLS
// #define LWIP_ALTCP_TLS_MBEDTLS
