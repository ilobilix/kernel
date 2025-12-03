// Copyright (C) 2024-2025  ilobilo

export module system.net:addr;
import std;

namespace net::addr
{
    template<std::size_t N, typename Type = std::uint8_t>
    struct base
    {
        std::array<Type, N> data;

        constexpr base() = default;
        constexpr base(auto ...data) requires (sizeof...(data) == N)
            : data { static_cast<Type>(data)... } { }

        base(std::byte *ptr)
        {
            for (std::size_t i = 0; i < N; i++)
                data[i] = reinterpret_cast<Type *>(ptr)[i];
        }

        base(std::uint8_t *ptr)
            : base(reinterpret_cast<std::byte *>(ptr)) { }

        std::byte *to_bytes(std::byte *ptr) const
        {
            auto dest = reinterpret_cast<Type *>(ptr);
            for (std::size_t i = 0; i < N; i++)
                *dest++ = data[i];

            return reinterpret_cast<std::byte *>(dest);
        }

        constexpr std::size_t size() const { return N; }

        constexpr Type &operator[](std::size_t i)
        {
            return data[i];
        }

        constexpr bool operator==(const base &rhs) const
        {
            return data == rhs.data;
        }
    };

    namespace ip
    {
        export struct v4 : base<4>
        {
            using base<4>::base;
            using base<4>::to_bytes;

            static constexpr v4 broadcast()
            {
                return { 0xFF, 0xFF, 0xFF, 0xFF };
            }
        };
    } // namespace ip

    export struct mac : base<6>
    {
        using base<6>::base;
        using base<6>::to_bytes;

        static constexpr mac broadcast()
        {
            return { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        }
    };
} // namespace net::addr

export namespace std
{
    template<>
    struct hash<net::addr::ip::v4>
    {
        std::uint64_t operator()(const net::addr::ip::v4 &val) const
        {
            std::uint64_t ret = val.data[3];
            ret |= static_cast<std::uint64_t>(val.data[2]) << 8;
            ret |= static_cast<std::uint64_t>(val.data[1]) << 16;
            ret |= static_cast<std::uint64_t>(val.data[0]) << 24;
            return ret;
        }
    };

    template<>
    struct hash<net::addr::mac>
    {
        std::uint64_t operator()(const net::addr::mac &val) const
        {
            std::uint64_t ret = val.data[5];
            ret |= static_cast<std::uint64_t>(val.data[4]) << 8;
            ret |= static_cast<std::uint64_t>(val.data[3]) << 16;
            ret |= static_cast<std::uint64_t>(val.data[2]) << 24;
            ret |= static_cast<std::uint64_t>(val.data[1]) << 32;
            ret |= static_cast<std::uint64_t>(val.data[0]) << 40;
            return ret;
        }
    };
} // export namespace std