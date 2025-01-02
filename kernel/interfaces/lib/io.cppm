// Copyright (C) 2024-2025  ilobilo

export module lib:io;

import :types;
import std;

#if defined(__x86_64__)
export import x86_64.lib.io;
export namespace lib::io
{
    constexpr bool supported = true;

    template<typename Type>
    inline Type in(auto port) { return lib::x86_64::io::in<Type>(port); }

    template<typename Type>
    inline void out(auto port, auto val) { lib::x86_64::io::out<Type>(port, val); }
} // export namespace lib::io
#else
export namespace lib::io
{
    constexpr bool supported = false;

    template<typename Type>
    inline Type in(auto) { static_assert(false, "io::in is not supported on this architecture"); return 0; }

    template<typename>
    inline void out(auto, auto) { static_assert(false, "io::out is not supported on this architecture"); }
} // export namespace lib::io
#endif

export namespace lib::io
{
    template<std::size_t N, typename Type = bits2uint_t<N>>
    inline Type in(auto port) { return in<Type>(port); }

    template<std::size_t N, typename Type = bits2uint_t<N>>
    inline void out(auto port, auto val) { out<Type>(port, val); }
} // export namespace lib::io