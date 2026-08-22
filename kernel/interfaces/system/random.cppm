// Copyright (C) 2024-2026  ilobilo

export module system.random;

import lib;
import std;

export namespace random
{
    void add_entropy(std::span<const std::byte> data);
    void add_irq_jitter(std::size_t vector, std::uintptr_t ip);

    std::uint32_t get_u32();
    std::uint64_t get_u64();

    std::ssize_t get_bytes(std::span<std::byte> buffer);
    std::ssize_t get_bytes(lib::maybe_uspan<std::byte> buffer);

    void get_uuid(std::span<std::uint8_t, 16> buffer);
} // export namespace random
