// Copyright (C) 2024-2026  ilobilo

export module lib:chacha20;

import std;

export namespace lib
{
    constexpr std::size_t chacha20_key_size = 32;
    constexpr std::size_t chacha20_nonce_size = 12;
    constexpr std::size_t chacha20_block_size = 64;

    void chacha20_block(
        std::span<const std::byte, chacha20_key_size> key,
        std::span<const std::byte, chacha20_nonce_size> nonce, std::uint32_t counter,
        std::span<std::byte, chacha20_block_size> out
    );
} // export namespace lib
