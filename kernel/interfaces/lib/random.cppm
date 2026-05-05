// Copyright (C) 2024-2026  ilobilo

export module lib:random;

import :user;
import std;

export namespace lib
{
    void add_entropy(std::span<const std::byte> data);
    void add_irq_jitter(std::size_t vector, std::uintptr_t ip);

    std::uint32_t get_random_u32();
    std::uint64_t get_random_u64();

    std::ssize_t random_bytes(lib::maybe_uspan<std::byte> buffer);
} // export namespace lib
