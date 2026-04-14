// Copyright (C) 2024-2025  ilobilo

export module system.net:ether;

import :addr;
import :packet;
import lib;
import std;

namespace utils
{
    inline constexpr bool is_type(std::uint16_t type_or_length)
    {
        return type_or_length > 0x05DC;
    }
} // namespace utils

export namespace net::ether
{
    enum class type : std::uint16_t
    {
        ipv4 = 0x0800,
        ipv6 = 0x86DD,
        arp = 0x0806
    };

    struct packet
    {
        addr::mac source;
        addr::mac dest;
        std::uint16_t tp;
        std::byte *data;
        std::size_t data_size;

        constexpr packet() = default;
        constexpr packet(addr::mac source, addr::mac dest, type tp) :
            source { source }, dest { dest }, tp { std::to_underlying(tp) },
            data { nullptr }, data_size { 0 } { }

        packet(std::byte *ptr, std::size_t ptr_length)
        {
            const auto bytes = reinterpret_cast<std::uint8_t *>(ptr);

            dest = addr::mac { bytes };
            source = addr::mac { bytes + 6 };

            tp = (static_cast<std::uint16_t>(bytes[12]) << 8) | bytes[13];

            data = reinterpret_cast<std::byte *>(bytes + 14);
            data_size = ptr_length - 18;
        }

        std::byte *to_bytes(std::byte *ptr) const
        {
            ptr = dest.to_bytes(ptr);
            ptr = source.to_bytes(ptr);

            *reinterpret_cast<std::uint8_t *>(ptr++) = (tp >> 8) & 0xFF;
            *reinterpret_cast<std::uint8_t *>(ptr++) = tp & 0xFF;

            return ptr;
        }

        constexpr bool is_valid() const
        {
            return utils::is_type(tp);
        }

        constexpr static std::size_t size()
        {
            return 14;
        }
    };

    template<is_processor ...Types>
        requires (std::same_as<typename Types::from_packet_type, packet> && ...)
    class dispatcher : sender
    {
        private:
        std::tuple<Types...> processors;
        addr::ip::v4 ip;
        nic *_nic;

        std::deque<lib::membuffer> packets;
        lib::semaphore event;

        void attach_senders() requires (sizeof...(Types) == 0) { }
        void attach_senders() requires (sizeof...(Types) > 0)
        {
            [&]<std::size_t ...I>(std::index_sequence<I...>) {
                (std::get<I>(processors).attach_sender(this), ...);
            } (std::make_index_sequence<sizeof...(Types)> { });
        }

        public:
        friend void swap(dispatcher &lhs, dispatcher &rhs)
        {
            using std::swap;
            swap(lhs.processors, rhs.processors);
            swap(lhs.ip, rhs.ip);
            swap(lhs._nic, rhs._nic);
            swap(lhs.packets, rhs.packets);
            swap(lhs.event, rhs.event);
        }

        dispatcher(nic *_nic) : processors { }, _nic { _nic } { attach_senders(); }
        dispatcher(dispatcher &&other) noexcept { swap(*this, other); }

        void runner(this dispatcher &self)
        {
            while (true)
            {
                if (self.packets.empty())
                    self.event.wait();

                while (self.packets.empty() == false)
                {
                    auto data = std::move(self.packets.pop_back_element());

                    auto frame = packet(data.virt_data(), data.size());
                    if (frame.is_valid() == false)
                        continue;

                    dispatch(std::move(data), std::move(frame), self.processors);
                }
            }
        }

        void send(lib::membuffer &&buffer) override
        {
            _nic->send(std::move(buffer));
        }

        void receive(lib::membuffer &&buffer) override
        {
            packets.emplace_front(std::move(buffer));
            event.signal();
        }

        addr::mac mac() override
        {
            return _nic->mac();
        }

        addr::ip::v4 ipv4() override
        {
            return ip;
        }

        void set_ipv4(addr::ip::v4 ip)
        {
            this->ip = ip;
        }

        template<std::size_t I>
        auto &nth_processor()
        {
            return std::get<I>(processors);
        }
    };
} // export namespace net::ether