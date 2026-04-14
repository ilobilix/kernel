// Copyright (C) 2024-2025  ilobilo

export module lib:types;
import std;

export namespace lib
{
    template<std::size_t N>
    struct bits2unit;

    template<>
    struct bits2unit<8> { using type = std::uint8_t; };

    template<>
    struct bits2unit<16> { using type = std::uint16_t; };

    template<>
    struct bits2unit<32> { using type = std::uint32_t; };

    template<>
    struct bits2unit<64> { using type = std::uint64_t; };

    template<std::size_t N>
    using bits2uint_t = bits2unit<N>::type;

    template<typename ...Funcs>
    struct overloaded : Funcs... { using Funcs::operator()...; };

    template<typename ...Funcs>
    overloaded(Funcs ...) -> overloaded<Funcs...>;
} // export namespace lib