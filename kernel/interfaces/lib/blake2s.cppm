// Copyright (C) 2024-2026  ilobilo

export module lib:blake2s;

import std;

export namespace lib
{
    constexpr std::size_t blake2s_block_size = 64;
    constexpr std::size_t blake2s_hash_size = 32;
    constexpr std::size_t blake2s_key_size = 32;

    struct blake2s_state
    {
        std::uint32_t h[8];
        std::uint32_t t[2];
        std::uint32_t f[2];
        std::byte buf[blake2s_block_size];
        std::size_t buflen;
        std::size_t outlen;
    };

    void blake2s_init(blake2s_state &state, std::size_t outlen);
    void blake2s_init_key(blake2s_state &state, std::size_t outlen, std::span<const std::byte> key);
    void blake2s_update(blake2s_state &state, std::span<const std::byte> input);
    void blake2s_final(blake2s_state &state, std::span<std::byte> out);

    void blake2s(
        std::span<std::byte> out, std::span<const std::byte> input,
        std::span<const std::byte> key = { }
    );
} // export namespace lib
