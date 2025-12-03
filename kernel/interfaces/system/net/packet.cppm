// Copyright (C) 2024-2025  ilobilo

export module system.net:packet;

import :addr;
import lib;
import std;

export namespace net
{
    class sender
    {
        public:
        virtual ~sender() { }

        virtual void send(lib::membuffer &&) = 0;
        virtual void receive(lib::membuffer &&) = 0;

        virtual addr::mac mac() = 0;
        virtual addr::ip::v4 ipv4() = 0;
    };

    class nic
    {
        protected:
        sender *receiver = nullptr;

        public:
        void attach_receiver(sender *receiver) { this->receiver = receiver; }

        virtual addr::mac mac() = 0;
        virtual bool send(lib::membuffer &&buffer) = 0;

        virtual ~nic() { }
    };

    template<typename Type>
    concept is_processor = requires (Type a, lib::membuffer &&b, typename Type::from_packet_type &&f, const typename Type::from_packet_type &f2, sender *s) {
        typename Type::from_packet_type;
        { a.attach_sender(s) };
        { a.push_packet(std::move(b), std::move(f)) };
        { a.matches(f2) } -> std::same_as<bool>;
    };

    template<typename Type>
    concept requires_checksum = requires (Type a, std::uint8_t *b, std::ptrdiff_t c, std::size_t d) {
        { a.do_checksum(b, c, d) } -> std::same_as<void>;
    };

    template<typename ...Args>
    lib::membuffer build_packet(Args &&...args)
    {
        auto parts = std::make_tuple(std::forward<Args>(args)...);

        const auto total_size = [&parts]<std::size_t ...I>(std::index_sequence<I...>) {
            return (std::get<I>(parts).size() + ...);
        } (std::make_index_sequence<sizeof...(Args)> { });

        lib::membuffer buffer(total_size);
        auto dest = buffer.virt_data();

        [&parts, &dest]<std::size_t ...I>(std::index_sequence<I...>) {
            return ((dest = std::get<I>(parts).to_bytes(dest)), ...);
        } (std::make_index_sequence<sizeof...(Args)> { });

        dest = buffer.virt_data();
        [&]<std::size_t ...I>(std::index_sequence<I...>)
        {
            ([&]<typename Type>(Type item)
            {
                if constexpr (requires_checksum<Type>)
                {
                    std::ptrdiff_t off = reinterpret_cast<std::uintptr_t>(dest) - reinterpret_cast<std::uintptr_t>(buffer.virt_data());
                    item.do_checksum(buffer.virt_data(), off, total_size);
                }
                dest += item.size();
            } (std::get<I>(parts)), ...);
        } (std::make_index_sequence<sizeof...(Args)> { });

        return std::move(buffer);
    }

    template<typename From>
    void dispatch(lib::membuffer &&, From &&, std::tuple<> &) { }

    template<typename From, is_processor ...Types>
    void dispatch(lib::membuffer &&buf, From &&fr, std::tuple<Types...> &prs)
    {
        [&]<std::size_t ...I>(std::index_sequence<I...>)
        {
            ([&]<typename Type>(Type &proc)
            {
                static_assert(std::is_same_v<From, typename Type::from_packet_type>);
                if (proc.matches(fr))
                {
                    proc.push_packet(std::move(buf), std::move(fr));
                    return false;
                }
                return true;
            } (std::get<I>(prs)) && ...);
        } (std::make_index_sequence<sizeof...(Types)> { });
    }

    void register_nic(std::unique_ptr<nic> device);
} // export namespace net