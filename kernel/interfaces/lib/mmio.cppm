// Copyright (C) 2024-2025  ilobilo

export module lib:mmio;

import :types;
import std;

export namespace lib::mmio
{
    template<std::unsigned_integral Type>
    inline Type in(auto addr)
    {
        volatile auto ptr = reinterpret_cast<volatile Type *>(addr);
        return *ptr;
    }

    template<std::unsigned_integral Type>
    inline void out(auto addr, Type val)
    {
        volatile auto ptr = reinterpret_cast<volatile Type *>(addr);
        *ptr = val;
    }

    template<std::size_t N, typename Type = bits2uint_t<N>>
    inline Type in(auto addr) { return in<Type>(addr); }

    template<std::size_t N, typename Type = bits2uint_t<N>>
    inline void out(auto addr, auto val) { out<Type>(addr, val); }
} // export namespace lib::mmio