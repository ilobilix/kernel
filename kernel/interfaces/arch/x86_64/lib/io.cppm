// Copyright (C) 2024-2025  ilobilo

export module x86_64.lib.io;

import magic_enum;
import std;

export namespace lib::x86_64::io
{
    template<typename Type>
    concept enum_or_int = std::is_enum_v<Type> || std::integral<Type>;

    template<std::unsigned_integral Type> requires (sizeof(Type) <= sizeof(std::uint32_t))
    inline Type in(enum_or_int auto port)
    {
        auto _port = static_cast<std::uint16_t>(port);
        Type data { 0 };

        if constexpr (std::same_as<Type, std::uint8_t>)
            asm volatile ("in %b0, %w1" : "=a"(data) : "Nd"(_port));
        else if constexpr (std::same_as<Type, std::uint16_t>)
            asm volatile ("in %w0, %w1" : "=a"(data) : "Nd"(_port));
        else if constexpr (std::same_as<Type, std::uint32_t>)
            asm volatile ("in %0, %w1" : "=a"(data) : "Nd"(_port));
        else
            static_assert(false, "io::in invalid size");

        return data;
    }

    template<std::unsigned_integral Type> requires (sizeof(Type) <= sizeof(std::uint32_t))
    inline void out(enum_or_int auto port, enum_or_int auto val)
    {
        auto _port = static_cast<std::uint16_t>(port);
        auto _val = static_cast<Type>(val);

        if constexpr (std::same_as<Type, std::uint8_t>)
            asm volatile ("out %w1, %b0" :: "a"(_val), "Nd"(_port));
        else if constexpr (std::same_as<Type, std::uint16_t>)
            asm volatile ("out %w1, %w0" :: "a"(_val), "Nd"(_port));
        else if constexpr (std::same_as<Type, std::uint32_t>)
            asm volatile ("out %w1, %0" :: "a"(_val), "Nd"(_port));
        else
            static_assert(false, "io::out invalid size");
    }
} // export namespace lib::x86_64::io