// Copyright (C) 2024-2025  ilobilo

export module lib:hash;
import :math;
import std;

export namespace lib::hash
{
    [[clang::no_sanitize("undefined")]]
    constexpr uint128_t murmur3_128(const void *key, std::uint64_t len, std::uint64_t seed, std::uint64_t *out = nullptr)
    {
        auto rotl64 = [](std::uint64_t x, std::int8_t r)
        {
            return (x << r) | (x >> (64 - r));
        };

        auto fmix64 = [](std::uint64_t k)
        {
            k ^= k >> 33;
            k *= 0xFF51AFD7ED558CCDull;
            k ^= k >> 33;
            k *= 0xC4CEB9FE1A85EC53ull;
            k ^= k >> 33;

            return k;
        };

        auto data = static_cast<const std::uint8_t *>(key);
        const std::uint64_t nblocks = len / 16;

        auto h1 = seed;
        auto h2 = seed;

        const auto c1 = 0x87C37B91114253D5ull;
        const auto c2 = 0x4CF5AD432745937Full;

        auto blocks = static_cast<const std::uint64_t *>(key);

        for (std::size_t i = 0; i < nblocks; i++)
        {
            auto k1 = blocks[i * 2 + 0];
            auto k2 = blocks[i * 2 + 1];

            k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
            h1 = rotl64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52DCE729;
            k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
            h2 = rotl64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495AB5;
        }

        auto tail = static_cast<const std::uint8_t *>(static_cast<const void *>(data + nblocks * 16));

        std::uint64_t k1 = 0;
        std::uint64_t k2 = 0;

        switch (len & 15)
        {
            case 15: k2 ^= (static_cast<std::uint64_t>(tail[14])) << 48;
            case 14: k2 ^= (static_cast<std::uint64_t>(tail[13])) << 40;
            case 13: k2 ^= (static_cast<std::uint64_t>(tail[12])) << 32;
            case 12: k2 ^= (static_cast<std::uint64_t>(tail[11])) << 24;
            case 11: k2 ^= (static_cast<std::uint64_t>(tail[10])) << 16;
            case 10: k2 ^= (static_cast<std::uint64_t>(tail[9])) << 8;
            case 9: k2 ^= (static_cast<std::uint64_t>(tail[8])) << 0;
                k2 *= c2; k2  = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
            case 8: k1 ^= (static_cast<std::uint64_t>(tail[7])) << 56;
            case 7: k1 ^= (static_cast<std::uint64_t>(tail[6])) << 48;
            case 6: k1 ^= (static_cast<std::uint64_t>(tail[5])) << 40;
            case 5: k1 ^= (static_cast<std::uint64_t>(tail[4])) << 32;
            case 4: k1 ^= (static_cast<std::uint64_t>(tail[3])) << 24;
            case 3: k1 ^= (static_cast<std::uint64_t>(tail[2])) << 16;
            case 2: k1 ^= (static_cast<std::uint64_t>(tail[1])) << 8;
            case 1: k1 ^= (static_cast<std::uint64_t>(tail[0])) << 0;
                k1 *= c1; k1  = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
        };

        h1 ^= len; h2 ^= len;

        h1 += h2;
        h2 += h1;

        h1 = fmix64(h1);
        h2 = fmix64(h2);

        h1 += h2;
        h2 += h1;

        if (out != nullptr)
        {
            out[0] = h1;
            out[1] = h2;
        }

        return (static_cast<uint128_t>(h1) << 64) | static_cast<uint128_t>(h2);
    }

    constexpr std::uint64_t murmur3_64(const void *key, std::uint64_t len, std::uint64_t seed)
    {
        auto val = murmur3_128(key, len, seed);
        return static_cast<std::uint64_t>(val) ^ static_cast<std::uint64_t>(val >> 64);
    }
} // export namespace lib::hash