// Copyright (C) 2024-2025  ilobilo

export module system.net:arp;

import :ether;
import :addr;
import :packet;

import lib;
import std;

namespace net::arp
{
    enum class hwtype : std::uint16_t
    {
        ethernet = 0x0001
    };

    enum class protocol : std::uint16_t
    {
        ipv4 = 0x0800
    };

    enum class opcode : std::uint16_t
    {
        request = 1,
        reply = 2,
        rrequest = 3,
        rreply = 4,
    };

    export struct packet
    {
        std::uint16_t hwtype;
        std::uint16_t protype;
        std::uint8_t hwsize;
        std::uint8_t prosize;
        std::uint16_t op;

        addr::mac smac;
        addr::ip::v4 sip;
        addr::mac dmac;
        addr::ip::v4 dip;

        constexpr packet(opcode op, addr::mac smac, addr::ip::v4 sip, addr::mac dmac, addr::ip::v4 dip) :
            hwtype { std::to_underlying(hwtype::ethernet) }, protype { std::to_underlying(protocol::ipv4) },
            hwsize { 6 }, prosize { 4 }, op { std::to_underlying(op) },
            smac { smac }, sip { sip }, dmac { dmac }, dip { dip } { }

        packet(const ether::packet &ether)
        {
            const auto bytes = reinterpret_cast<std::uint8_t *>(ether.data);

            hwtype = (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1];
            protype = (static_cast<std::uint16_t>(bytes[2]) << 8) | bytes[3];

            hwsize = bytes[4];
            prosize = bytes[5];
            op = (static_cast<std::uint16_t>(bytes[6]) << 8) | bytes[7];

            lib::bug_on(hwtype != std::to_underlying(hwtype::ethernet));
            lib::bug_on(protype != std::to_underlying(protocol::ipv4));
            lib::bug_on(hwsize != 6);
            lib::bug_on(prosize != 4);

            smac = addr::mac { bytes + 8 };
            sip = addr::ip::v4 { bytes + 14 };
            dmac = addr::mac { bytes + 18 };
            dip = addr::ip::v4 { bytes + 24 };
        }

        std::byte *to_bytes(std::byte *ptr) const
        {
            lib::bug_on(hwtype != std::to_underlying(hwtype::ethernet));
            lib::bug_on(protype != std::to_underlying(protocol::ipv4));
            lib::bug_on(hwsize != 6);
            lib::bug_on(prosize != 4);

            auto bytes = reinterpret_cast<std::uint8_t *>(ptr);

            *bytes++ = (hwtype >> 8) & 0xFF;
            *bytes++ = hwtype & 0xFF;
            *bytes++ = (protype >> 8) & 0xFF;
            *bytes++ = protype & 0xFF;
            *bytes++ = hwsize;
            *bytes++ = prosize;

            *bytes++ = (op >> 8) & 0xFF;
            *bytes++ = op & 0xFF;

            ptr = reinterpret_cast<std::byte *>(bytes);

            ptr = smac.to_bytes(ptr);
            ptr = sip.to_bytes(ptr);
            ptr = dmac.to_bytes(ptr);
            ptr = dip.to_bytes(ptr);

            return ptr;
        }

        constexpr std::size_t size() { return 8 + hwsize * 2 + prosize * 2; }

        static constexpr packet query_for(addr::mac smac, addr::ip::v4 sip, addr::ip::v4 dip)
        {
            return { opcode::request, smac, sip, { }, dip };
        }

        static constexpr packet reply_for(addr::mac smac, addr::ip::v4 sip, addr::mac dmac, addr::ip::v4 dip)
        {
            return { opcode::reply, smac, sip, dmac, dip };
        }
    };

    struct route
    {
        addr::ip::v4 ip;
        addr::mac mac;
        bool resolved;

        lib::semaphore doorbell;

        constexpr bool operator==(const route &rhs) const
        {
            return ip == rhs.ip && mac == rhs.mac;
        }
    };

    struct processor
    {
        private:
        // TODO: hashmap
        std::vector<std::unique_ptr<route>> routes {
            std::make_unique<route>(addr::ip::v4::broadcast(), addr::mac::broadcast(), true)
        };
        sender *_sender = nullptr;

        void submit_query(addr::ip::v4 dip)
        {
            _sender->send(build_packet(
                ether::packet { _sender->mac(), addr::mac::broadcast(), ether::type::arp },
                packet::query_for(_sender->mac(), _sender->ipv4(), dip)
            ));
        }

        void submit_reply(addr::mac dmac, addr::ip::v4 dip)
        {
            _sender->send(build_packet(
                ether::packet { _sender->mac(), dmac, ether::type::arp },
                packet::reply_for(_sender->mac(), _sender->ipv4(), dmac, dip)
            ));
        }

        public:
        using from_packet_type = ether::packet;

        void attach_sender(sender *s) { _sender = s; }

        void push_packet(lib::membuffer &&, ether::packet &&ether)
        {
            packet arp { ether };
            switch (static_cast<opcode>(arp.op))
            {
                case opcode::request:
                    if (arp.dip == _sender->ipv4())
                        submit_reply(arp.smac, arp.sip);
                    break;
                case opcode::reply:
                    for (auto &route : routes)
                    {
                        if (route->ip != arp.sip)
                            continue;

                        route->ip = arp.sip;
                        route->mac = arp.smac;
                        route->resolved = true;
                        route->doorbell.signal();
                    }
                    break;
                default:
                    break;
            }
        }

        addr::mac mac_of(addr::ip::v4 ipv4)
        {
            for (auto &route : routes)
            {
                if (route->ip != ipv4)
                    continue;

                route->doorbell.wait();
                lib::bug_on(route->resolved == false);
                return route->mac;
            }

            const auto &route = routes.emplace_back(new arp::route(ipv4, { }, false));
            submit_query(ipv4);

            route->doorbell.wait();
            lib::bug_on(route->resolved == false);
            return route->mac;
        }

        constexpr bool matches(const ether::packet &ether)
        {
            return static_cast<ether::type>(ether.tp) == ether::type::arp;
        }
    };
    static_assert(is_processor<processor>);
} // namespace net::arp