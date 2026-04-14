// Copyright (C) 2024-2025  ilobilo

export module lib:mmio;

import :types;
import std;

extern "C++" namespace lib::mmio
{
    std::uint64_t read(std::uintptr_t addr, std::size_t width);
    void write(std::uintptr_t addr, std::uint64_t val, std::size_t width);
} // extern "C++" namespace lib::mmio

export namespace lib::mmio
{
    template<std::unsigned_integral Type>
    inline Type in(auto addr)
    {
        return read(addr, sizeof(Type));
    }

    template<std::unsigned_integral Type>
    inline void out(auto addr, Type val)
    {
        write(addr, val, sizeof(Type));
    }

    template<std::size_t N, typename Type = bits2uint_t<N>>
    inline Type in(auto addr) { return in<Type>(addr); }

    template<std::size_t N, typename Type = bits2uint_t<N>>
    inline void out(auto addr, auto val) { out<Type>(addr, val); }
} // export namespace lib::mmio