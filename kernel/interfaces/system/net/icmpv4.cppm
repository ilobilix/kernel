// Copyright (C) 2024-2025  ilobilo

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

        std::uint8_t *payload;
        std::size_t payload_size;

        packet(const ipv4::packet &data)
        {
            const auto bytes = reinterpret_cast<std::uint8_t *>(data.data);
            type = bytes[0];
            code = bytes[1];
            csum = (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1];

            switch (static_cast<enum type>(type))
            {
                case type::echo_request:
                case type::echo_reply:
                    ident = (static_cast<std::uint16_t>(bytes[4]) << 8) | bytes[5];
                    seq = (static_cast<std::uint16_t>(bytes[6]) << 8) | bytes[7];
                    payload = const_cast<std::uint8_t *>(bytes) + 8;
                    payload_size = data.data_size - 8;
                    break;
                default:
                    break;
            }
        }

        std::uint8_t *to_bytes(std::uint8_t *ptr)
        {
            *ptr++ = type;
            *ptr++ = code;

            // checksum
            *ptr++ = 0;
            *ptr++ = 0;

            switch (static_cast<enum type>(type))
            {
                case type::echo_request:
                case type::echo_reply:
                    *ptr++ = ident >> 8;
                    *ptr++ = ident & 0xFF;

                    *ptr++ = seq >> 8;
                    *ptr++ = seq & 0xFF;

                    std::memcpy(ptr, payload, payload_size);
                    ptr += payload_size;
                    break;
                default:
                    break;
            }

            return ptr;
        }

        void do_checksum(std::uint8_t *buf, std::ptrdiff_t offset, std::size_t len)
        {
            lib::unused(len);
            const auto hdr = buf + offset;
            const auto sum = checksum::compute(reinterpret_cast<std::byte *>(hdr), size());
            hdr[2] = sum >> 8;
            hdr[3] = sum & 0xFF;
        }

        constexpr std::size_t size()
        {
            switch (static_cast<enum type>(type))
            {
                case type::echo_request:
                case type::echo_reply:
                    return payload_size + 8;
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

        void push_packet(lib::membuffer &&buffer, ipv4::packet &&packet);
        bool matches(const ipv4::packet &packet);
    };
    static_assert(is_processor<processor>);
} // namespace net::ipv4::icmp