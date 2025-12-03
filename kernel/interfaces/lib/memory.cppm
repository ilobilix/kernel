// Copyright (C) 2024-2025  ilobilo

export module lib:memory;
import std;

namespace lib::detail
{
    void *alloc(std::size_t size);
    void *allocz(std::size_t size);
    void *realloc(void *oldptr, std::size_t size);
    void free(void *ptr);
} // namespace lib::detail

export namespace lib
{
    template<typename Type = void *>
    inline Type alloc(std::size_t size)
    {
        return reinterpret_cast<Type>(detail::alloc(size));
    }

    template<typename Type = void *>
    inline Type allocz(std::size_t size)
    {
        return reinterpret_cast<Type>(detail::allocz(size));
    }

    template<typename Type>
    inline Type realloc(Type oldptr, std::size_t size)
    {
        return reinterpret_cast<Type>(detail::realloc(reinterpret_cast<void *>(oldptr), size));
    }

    inline void free(auto ptr)
    {
        detail::free(reinterpret_cast<void *>(ptr));
    }
} // export namespace lib