// Copyright (C) 2024-2025  ilobilo

export module system.net:ipv4;

import :addr;
import :checksum;
import :ether;
import :packet;

import lib;
import std;

namespace net::ipv4
{
    enum class protocol : std::uint8_t
    {
        icmp = 0x01,
        tcp = 0x06,
        udp = 0x11
    };

    struct packet
    {
        addr::ip::v4 sip;
        addr::ip::v4 dip;
        std::uint8_t proto;
        std::uint8_t ttl;
        std::uint16_t length;
        std::uint8_t ihl;

        std::byte *data;
        std::size_t data_size;

        ether::packet ether;

        packet(const ether::packet &ether) : ether { ether }
        {
            const auto bytes = reinterpret_cast<std::uint8_t *>(ether.data);

            sip = addr::ip::v4 { bytes + 12 };
            dip = addr::ip::v4 { bytes + 16 };
            ihl = bytes[0] & 0xF;
            ttl = bytes[8];
            proto = bytes[9];
            length = (static_cast<std::uint16_t>(bytes[2]) << 8) | bytes[3];

            data = reinterpret_cast<std::byte *>(bytes + (ihl * 4));
            data_size = length - (ihl * 4);
        }

        std::byte *to_bytes(std::byte *ptr) const
        {
            // TODO: options
            if (ihl != 5)
                lib::panic("ipv4: options not supported");

            auto bytes = reinterpret_cast<std::uint8_t *>(ptr);

            *bytes++ = (4 << 4) | ihl;
            *bytes++ = 0;

            const auto size = data_size + (ihl * 4);
            *bytes++ = size >> 8;
            *bytes++ = size & 0xFF;

            // ID
            *bytes++ = 0;
            *bytes++ = 0;

            // flags : 3, frag_offset : 13
            *bytes++ = 0;
            *bytes++ = 0;

            *bytes++ = ttl;
            *bytes++ = proto;

            // checksum
            *bytes++ = 0;
            *bytes++ = 0;

            ptr = reinterpret_cast<std::byte *>(bytes);

            ptr = sip.to_bytes(ptr);
            ptr = dip.to_bytes(ptr);

            return ptr;
        }

        void do_checksum(std::byte *ptr, std::ptrdiff_t offset, std::size_t len)
        {
            const auto bytes = reinterpret_cast<std::byte *>(ptr + offset);
            auto hdr = reinterpret_cast<std::uint8_t *>(bytes);

            len -= offset;
            hdr[2] = len >> 8;
            hdr[3] = len & 0xFF;

            const auto sum = checksum::compute(bytes, size());

            hdr[10] = sum >> 8;
            hdr[11] = sum & 0xFF;
        }

        constexpr std::size_t size() { return ihl * 4; }
    };

    template<typename ...Types>
        requires (std::same_as<typename Types::from_frame_type, packet> && ...)
    class processor
    {
        private:
        std::tuple<Types...> processors;

        public:
        using from_packet_type = ether::packet;

        void attach_sender(sender *) requires (sizeof...(Types) == 0) { }
        void attach_sender(sender *s) requires (sizeof...(Types) > 0)
        {
            [&]<std::size_t ...I>(std::index_sequence<I...>) {
                (std::get<I>(processors).attach_sender(s), ...);
            } (std::make_index_sequence<sizeof...(Types)> { });
        }

        void push_packet(lib::membuffer &&buffer, ether::packet &&eth)
        {
            dispatch(std::move(buffer), packet { eth }, processors);
        }

        bool matches(const ether::packet &eth)
        {
            return eth.tp == std::to_underlying(ether::type::ipv4);
        }

        template<std::size_t I>
        auto &nth_processor()
        {
            return std::get<I>(processors);
        }
    };
    static_assert(is_processor<processor<>>);
} // namespace net::ipv4