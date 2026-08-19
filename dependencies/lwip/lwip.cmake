# Copyright (C) 2024-2026  ilobilo

set(LWIP_DIR "${CMAKE_CURRENT_LIST_DIR}/lwip")

set(LWIP_INCLUDES
    "${LWIP_DIR}/src/include"
    "${CMAKE_SOURCE_DIR}/kernel/include/kernel/lwip"
    CACHE INTERNAL ""
)
set(LWIP_SOURCES
    # core
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/dns.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/raw.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/altcp.c
    ${LWIP_DIR}/src/core/altcp_alloc.c
    ${LWIP_DIR}/src/core/altcp_tcp.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c

    # core/ipv4
    ${LWIP_DIR}/src/core/ipv4/acd.c
    ${LWIP_DIR}/src/core/ipv4/autoip.c
    # ${LWIP_DIR}/src/core/ipv4/dhcp.c
    ${LWIP_DIR}/src/core/ipv4/etharp.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/igmp.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c

    # core/ipv6
    # ${LWIP_DIR}/src/core/ipv6/dhcp6.c
    ${LWIP_DIR}/src/core/ipv6/ethip6.c
    ${LWIP_DIR}/src/core/ipv6/icmp6.c
    ${LWIP_DIR}/src/core/ipv6/inet6.c
    ${LWIP_DIR}/src/core/ipv6/ip6.c
    ${LWIP_DIR}/src/core/ipv6/ip6_addr.c
    ${LWIP_DIR}/src/core/ipv6/ip6_frag.c
    ${LWIP_DIR}/src/core/ipv6/mld6.c
    ${LWIP_DIR}/src/core/ipv6/nd6.c

    # sequential and socket api
    ${LWIP_DIR}/src/api/api_lib.c
    ${LWIP_DIR}/src/api/api_msg.c
    ${LWIP_DIR}/src/api/err.c
    ${LWIP_DIR}/src/api/netbuf.c
    ${LWIP_DIR}/src/api/netifapi.c
    ${LWIP_DIR}/src/api/tcpip.c
    # ${LWIP_DIR}/src/api/if_api.c
    # ${LWIP_DIR}/src/api/netdb.c
    # ${LWIP_DIR}/src/api/sockets.c

    # netif
    ${LWIP_DIR}/src/netif/ethernet.c
    # ${LWIP_DIR}/src/netif/bridgeif.c
    # ${LWIP_DIR}/src/netif/bridgeif_fdb.c
    # ${LWIP_DIR}/src/netif/slipif.c

    # 6LoWPAN
    # ${LWIP_DIR}/src/netif/lowpan6_common.c
    # ${LWIP_DIR}/src/netif/lowpan6.c
    # ${LWIP_DIR}/src/netif/lowpan6_ble.c
    # ${LWIP_DIR}/src/netif/zepif.c

    # PPP
    # ${LWIP_DIR}/src/netif/ppp/auth.c
    # ${LWIP_DIR}/src/netif/ppp/ccp.c
    # ${LWIP_DIR}/src/netif/ppp/chap-md5.c
    # ${LWIP_DIR}/src/netif/ppp/chap_ms.c
    # ${LWIP_DIR}/src/netif/ppp/chap-new.c
    # ${LWIP_DIR}/src/netif/ppp/demand.c
    # ${LWIP_DIR}/src/netif/ppp/eap.c
    # ${LWIP_DIR}/src/netif/ppp/ecp.c
    # ${LWIP_DIR}/src/netif/ppp/eui64.c
    # ${LWIP_DIR}/src/netif/ppp/fsm.c
    # ${LWIP_DIR}/src/netif/ppp/ipcp.c
    # ${LWIP_DIR}/src/netif/ppp/ipv6cp.c
    # ${LWIP_DIR}/src/netif/ppp/lcp.c
    # ${LWIP_DIR}/src/netif/ppp/magic.c
    # ${LWIP_DIR}/src/netif/ppp/mppe.c
    # ${LWIP_DIR}/src/netif/ppp/multilink.c
    # ${LWIP_DIR}/src/netif/ppp/ppp.c
    # ${LWIP_DIR}/src/netif/ppp/pppapi.c
    # ${LWIP_DIR}/src/netif/ppp/pppcrypt.c
    # ${LWIP_DIR}/src/netif/ppp/pppoe.c
    # ${LWIP_DIR}/src/netif/ppp/pppol2tp.c
    # ${LWIP_DIR}/src/netif/ppp/pppos.c
    # ${LWIP_DIR}/src/netif/ppp/upap.c
    # ${LWIP_DIR}/src/netif/ppp/utils.c
    # ${LWIP_DIR}/src/netif/ppp/vj.c
    # ${LWIP_DIR}/src/netif/ppp/polarssl/arc4.c
    # ${LWIP_DIR}/src/netif/ppp/polarssl/des.c
    # ${LWIP_DIR}/src/netif/ppp/polarssl/md4.c
    # ${LWIP_DIR}/src/netif/ppp/polarssl/md5.c
    # ${LWIP_DIR}/src/netif/ppp/polarssl/sha1.c

    # apps/snmp
    # ${LWIP_DIR}/src/apps/snmp/snmp_asn1.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_core.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_icmp.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_interfaces.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_ip.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_snmp.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_system.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_tcp.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_mib2_udp.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_snmpv2_framework.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_snmpv2_usm.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_msg.c
    # ${LWIP_DIR}/src/apps/snmp/snmpv3.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_netconn.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_pbuf_stream.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_raw.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_scalar.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_table.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_threadsync.c
    # ${LWIP_DIR}/src/apps/snmp/snmp_traps.c

    # apps/http
    # ${LWIP_DIR}/src/apps/http/altcp_proxyconnect.c
    # ${LWIP_DIR}/src/apps/http/fs.c
    # ${LWIP_DIR}/src/apps/http/http_client.c
    # ${LWIP_DIR}/src/apps/http/httpd.c

    # apps/iperf
    # ${LWIP_DIR}/src/apps/lwiperf/lwiperf.c

    # apps/smtp
    # ${LWIP_DIR}/src/apps/smtp/smtp.c

    # apps/sntp
    # ${LWIP_DIR}/src/apps/sntp/sntp.c

    # apps/mdns
    # ${LWIP_DIR}/src/apps/mdns/mdns.c
    # ${LWIP_DIR}/src/apps/mdns/mdns_out.c
    # ${LWIP_DIR}/src/apps/mdns/mdns_domain.c

    # apps/netbiosns
    # ${LWIP_DIR}/src/apps/netbiosns/netbiosns.c

    # apps/tftp
    # ${LWIP_DIR}/src/apps/tftp/tftp.c

    # apps/mqtt
    # ${LWIP_DIR}/src/apps/mqtt/mqtt.c

    # apps/altcp_tls mbedTLS
    # ${LWIP_DIR}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    # ${LWIP_DIR}/src/apps/altcp_tls/altcp_tls_mbedtls_mem.c
    # ${LWIP_DIR}/src/apps/snmp/snmpv3_mbedtls.c

    CACHE INTERNAL ""
)
