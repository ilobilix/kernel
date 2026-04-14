// Copyright (C) 2024-2026  ilobilo

export module system.net:icmpv4;

import :ipv4;
import :packet;
import lib;
import std;

namespace net::ipv4::icmp
{
    enum class type : std::uint8_t
    {
        echo_request = 8,
        echo_reply = 0
    };

    struct packet
    {
        std::uint8_t type;
        std::uint8_t code;
        std::uint16_t csum;

        std::uint16_t ident;
        std::uint16_t seq;

        std::byte *data;
        std::size_t data_size;

        packet(enum type typ, std::uint16_t ident, std::uint16_t seq, std::byte *data, std::size_t size)
            : type { std::to_underlying(typ) }, ident { ident }, seq { seq },
              data { data }, data_size { size } { }

        packet(const ipv4::packet &packet)
        {
            const auto bytes = reinterpret_cast<std::uint8_t *>(packet.data);
            type = bytes[0];
            code = bytes[1];
            csum = (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1];

            switch (static_cast<enum type>(type))
            {
                case type::echo_request:
                case type::echo_reply:
                    ident = (static_cast<std::uint16_t>(bytes[4]) << 8) | bytes[5];
                    seq = (static_cast<std::uint16_t>(bytes[6]) << 8) | bytes[7];
                    data = const_cast<std::byte *>(reinterpret_cast<const std::byte *>(bytes) + 8);
                    data_size = packet.data_size - 8;
                    break;
                default:
                    break;
            }
        }

        std::byte *to_bytes(std::byte *ptr)
        {
            auto bytes = reinterpret_cast<std::uint8_t *>(ptr);

            *bytes++ = type;
            *bytes++ = code;

            // checksum
            *bytes++ = 0;
            *bytes++ = 0;

            switch (static_cast<enum type>(type))
            {
                case type::echo_request:
                case type::echo_reply:
                    *bytes++ = ident >> 8;
                    *bytes++ = ident & 0xFF;

                    *bytes++ = seq >> 8;
                    *bytes++ = seq & 0xFF;

                    std::memcpy(bytes, data, data_size);
                    bytes += data_size;
                    break;
                default:
                    break;
            }

            return ptr;
        }

        void do_checksum(std::byte *buf, std::ptrdiff_t offset, std::size_t len)
        {
            lib::unused(len);

            const auto bytes = buf + offset;
            const auto sum = checksum::compute(bytes, size());

            auto hdr = reinterpret_cast<std::uint8_t *>(bytes);
            hdr[2] = sum >> 8;
            hdr[3] = sum & 0xFF;
        }

        constexpr std::size_t size()
        {
            switch (static_cast<enum type>(type))
            {
                case type::echo_request:
                case type::echo_reply:
                    return data_size + 8;
                default:
                    return 4;
            }
            std::unreachable();
        }
    };

    struct processor
    {
        private:
        sender *_sender;

        public:
        using from_packet_type = ipv4::packet;

        processor() : _sender { nullptr } { }

        void attach_sender(sender *s) { _sender = s; }

        void push_packet(lib::membuffer &&buffer, ipv4::packet &&pckt)
        {
            lib::unused(buffer);

            packet icmp { pckt };
            if (icmp.type == std::to_underlying(type::echo_request))
            {
                if (pckt.dip != _sender->ipv4())
                    return;

                _sender->send(build_packet(
                    ether::packet { _sender->mac(), pckt.ether.source, ether::type::ipv4 },
                    ipv4::packet { _sender->ipv4(), pckt.sip, ipv4::protocol::icmp, 64 },
                    packet { type::echo_reply, icmp.ident, icmp.seq, icmp.data, icmp.data_size }
                ));
            }
        }

        bool matches(const ipv4::packet &packet)
        {
            return packet.proto == std::to_underlying(ipv4::protocol::icmp);
        }
    };
    static_assert(is_processor<processor>);
} // namespace net::ipv4::icmp
