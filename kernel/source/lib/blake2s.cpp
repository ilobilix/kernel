// Copyright (C) 2024-2026  ilobilo

module lib;

namespace lib
{
    namespace
    {
        constexpr std::array<std::uint32_t, 8> blake2s_iv {
            0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
            0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
        };

        constexpr std::array<std::array<std::uint8_t, 16>, 10> sigma { {
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
            { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
            { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
            { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
            { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
            { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
            { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
            { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
            { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
            { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 }
        } };

        constexpr std::uint32_t rotr32(std::uint32_t x, unsigned n)
        {
            return (x >> n) | (x << (32 - n));
        }

        constexpr std::uint32_t load_u32_le(const std::byte *ptr)
        {
            return  static_cast<std::uint32_t>(ptr[0])
                 | (static_cast<std::uint32_t>(ptr[1]) << 8)
                 | (static_cast<std::uint32_t>(ptr[2]) << 16)
                 | (static_cast<std::uint32_t>(ptr[3]) << 24);
        }

        constexpr void store_u32_le(std::byte *ptr, std::uint32_t val)
        {
            ptr[0] = static_cast<std::byte>(val & 0xFF);
            ptr[1] = static_cast<std::byte>((val >> 8) & 0xFF);
            ptr[2] = static_cast<std::byte>((val >> 16) & 0xFF);
            ptr[3] = static_cast<std::byte>((val >> 24) & 0xFF);
        }

        constexpr void g_mix(
            std::array<std::uint32_t, 16> &v,
            std::size_t a, std::size_t b, std::size_t c, std::size_t d,
            std::uint32_t x, std::uint32_t y
        ) {
            v[a] += v[b] + x; v[d] = rotr32(v[d] ^ v[a], 16);
            v[c] += v[d];     v[b] = rotr32(v[b] ^ v[c], 12);
            v[a] += v[b] + y; v[d] = rotr32(v[d] ^ v[a], 8);
            v[c] += v[d];     v[b] = rotr32(v[b] ^ v[c], 7);
        }

        void increment_counter(blake2s_state &state, std::uint32_t inc)
        {
            state.t[0] += inc;
            state.t[1] += (state.t[0] < inc);
        }

        void compress(blake2s_state &state, const std::byte *block)
        {
            std::array<std::uint32_t, 16> m;
            for (std::size_t i = 0; i < 16; i++)
                m[i] = load_u32_le(block + i * 4);

            std::array<std::uint32_t, 16> v;
            for (std::size_t i = 0; i < 8; i++)
            {
                v[i] = state.h[i];
                v[i + 8] = blake2s_iv[i];
            }

            v[12] ^= state.t[0];
            v[13] ^= state.t[1];
            v[14] ^= state.f[0];
            v[15] ^= state.f[1];

            for (std::size_t r = 0; r < 10; r++)
            {
                const auto &s = sigma[r];
                g_mix(v, 0, 4, 8,  12, m[s[0]],  m[s[1]]);
                g_mix(v, 1, 5, 9,  13, m[s[2]],  m[s[3]]);
                g_mix(v, 2, 6, 10, 14, m[s[4]],  m[s[5]]);
                g_mix(v, 3, 7, 11, 15, m[s[6]],  m[s[7]]);
                g_mix(v, 0, 5, 10, 15, m[s[8]],  m[s[9]]);
                g_mix(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
                g_mix(v, 2, 7, 8,  13, m[s[12]], m[s[13]]);
                g_mix(v, 3, 4, 9,  14, m[s[14]], m[s[15]]);
            }

            for (std::size_t i = 0; i < 8; i++)
                state.h[i] ^= v[i] ^ v[i + 8];
        }
    } // namespace

    void blake2s_init(blake2s_state &state, std::size_t outlen)
    {
        state = { };
        for (std::size_t i = 0; i < 8; i++)
            state.h[i] = blake2s_iv[i];
        state.h[0] ^= 0x01010000 ^ static_cast<std::uint32_t>(outlen);
        state.outlen = outlen;
    }

    void blake2s_init_key(
        blake2s_state &state,
        std::size_t outlen,
        std::span<const std::byte> key
    ) {
        state = { };
        for (std::size_t i = 0; i < 8; i++)
            state.h[i] = blake2s_iv[i];

        state.h[0] ^= 0x01010000 ^ (static_cast<std::uint32_t>(key.size()) << 8)
                    ^ static_cast<std::uint32_t>(outlen);
        state.outlen = outlen;

        if (!key.empty())
        {
            std::array<std::byte, blake2s_block_size> block { };
            std::copy(key.begin(), key.end(), block.begin());
            blake2s_update(state, block);
        }
    }

    void blake2s_update(blake2s_state &state, std::span<const std::byte> input)
    {
        if (input.empty())
            return;

        const std::size_t fill = blake2s_block_size - state.buflen;

        if (input.size() > fill)
        {
            std::copy_n(input.begin(), fill, state.buf + state.buflen);
            increment_counter(state, blake2s_block_size);
            compress(state, state.buf);
            state.buflen = 0;
            input = input.subspan(fill);

            while (input.size() > blake2s_block_size)
            {
                increment_counter(state, blake2s_block_size);
                compress(state, input.data());
                input = input.subspan(blake2s_block_size);
            }
        }

        std::copy(input.begin(), input.end(), state.buf + state.buflen);
        state.buflen += input.size();
    }

    void blake2s_final(blake2s_state &state, std::span<std::byte> out)
    {
        increment_counter(state, static_cast<std::uint32_t>(state.buflen));
        state.f[0] = 0xFFFFFFFF;
        for (std::size_t i = state.buflen; i < blake2s_block_size; i++)
            state.buf[i] = std::byte { 0 };
        compress(state, state.buf);

        std::array<std::byte, blake2s_hash_size> digest;
        for (std::size_t i = 0; i < 8; i++)
            store_u32_le(&digest[i * 4], state.h[i]);

        const std::size_t copylen = std::min(out.size(), state.outlen);
        std::copy_n(digest.begin(), copylen, out.begin());
    }

    void blake2s(
        std::span<std::byte> out,
        std::span<const std::byte> input,
        std::span<const std::byte> key
    ) {
        blake2s_state state;
        if (key.empty())
            blake2s_init(state, out.size());
        else
            blake2s_init_key(state, out.size(), key);
        blake2s_update(state, input);
        blake2s_final(state, out);
    }
} // namespace lib
