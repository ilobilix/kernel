// Copyright (C) 2024-2026  ilobilo

module lib;

namespace lib
{
    namespace
    {
        constexpr std::array<std::uint32_t, 4> sigma {
            0x61707865, 0x3320646e, 0x79622d32, 0x6b206574
        };

        constexpr std::uint32_t rotl32(std::uint32_t x, unsigned n)
        {
            return (x << n) | (x >> (32 - n));
        }

        constexpr void quarter_round(
            std::uint32_t &a, std::uint32_t &b, std::uint32_t &c, std::uint32_t &d
        )
        {
            a += b;
            d ^= a;
            d = rotl32(d, 16);
            c += d;
            b ^= c;
            b = rotl32(b, 12);
            a += b;
            d ^= a;
            d = rotl32(d, 8);
            c += d;
            b ^= c;
            b = rotl32(b, 7);
        }

        constexpr std::uint32_t load_u32_le(const std::byte *ptr)
        {
            return static_cast<std::uint32_t>(ptr[0]) | (static_cast<std::uint32_t>(ptr[1]) << 8) |
                (static_cast<std::uint32_t>(ptr[2]) << 16) |
                (static_cast<std::uint32_t>(ptr[3]) << 24);
        }

        constexpr void store_u32_le(std::byte *ptr, std::uint32_t val)
        {
            ptr[0] = static_cast<std::byte>(val & 0xFF);
            ptr[1] = static_cast<std::byte>((val >> 8) & 0xFF);
            ptr[2] = static_cast<std::byte>((val >> 16) & 0xFF);
            ptr[3] = static_cast<std::byte>((val >> 24) & 0xFF);
        }
    } // namespace

    void chacha20_block(
        std::span<const std::byte, chacha20_key_size> key,
        std::span<const std::byte, chacha20_nonce_size> nonce, std::uint32_t counter,
        std::span<std::byte, chacha20_block_size> out
    )
    {
        const std::array<std::uint32_t, 16> state {
            sigma[0],
            sigma[1],
            sigma[2],
            sigma[3],
            load_u32_le(&key[0]),
            load_u32_le(&key[4]),
            load_u32_le(&key[8]),
            load_u32_le(&key[12]),
            load_u32_le(&key[16]),
            load_u32_le(&key[20]),
            load_u32_le(&key[24]),
            load_u32_le(&key[28]),
            counter,
            load_u32_le(&nonce[0]),
            load_u32_le(&nonce[4]),
            load_u32_le(&nonce[8])
        };

        auto x = state;
        for (std::size_t i = 0; i < 10; i++)
        {
            quarter_round(x[0], x[4], x[8], x[12]);
            quarter_round(x[1], x[5], x[9], x[13]);
            quarter_round(x[2], x[6], x[10], x[14]);
            quarter_round(x[3], x[7], x[11], x[15]);

            quarter_round(x[0], x[5], x[10], x[15]);
            quarter_round(x[1], x[6], x[11], x[12]);
            quarter_round(x[2], x[7], x[8], x[13]);
            quarter_round(x[3], x[4], x[9], x[14]);
        }

        for (std::size_t i = 0; i < 16; i++)
            store_u32_le(&out[i * 4], x[i] + state[i]);
    }
} // namespace lib
