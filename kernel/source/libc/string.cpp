// Copyright (C) 2024-2025  ilobilo

import lib;
import std;

// phd memcpy from managarm

template<typename Type>
struct word_helper { enum class [[gnu::may_alias, gnu::aligned(1)]] word_enum : Type { }; };

template<typename Type>
using word = typename word_helper<Type>::word_enum;

template<typename Type>
[[gnu::always_inline, gnu::artificial]]
inline word<Type> alias_load(const std::uint8_t *&p)
{
    word<Type> value = *reinterpret_cast<const word<Type> *>(p);
    p += sizeof(Type);
    return value;
}

template<typename Type>
[[gnu::always_inline, gnu::artificial]]
inline void alias_store(std::uint8_t *&p, word<Type> value)
{
    *reinterpret_cast<word<Type> *>(p) = value;
    p += sizeof(Type);
}

extern "C"
{
    void *memcpy(void *dest, const void *src, std::size_t n)
    {
        auto cur_dest = reinterpret_cast<std::uint8_t *>(dest);
        auto curSrc = reinterpret_cast<const std::uint8_t *>(src);

        while (n >= 8 * 8)
        {
            auto w1 = alias_load<std::uint64_t>(curSrc);
            auto w2 = alias_load<std::uint64_t>(curSrc);
            auto w3 = alias_load<std::uint64_t>(curSrc);
            auto w4 = alias_load<std::uint64_t>(curSrc);
            auto w5 = alias_load<std::uint64_t>(curSrc);
            auto w6 = alias_load<std::uint64_t>(curSrc);
            auto w7 = alias_load<std::uint64_t>(curSrc);
            auto w8 = alias_load<std::uint64_t>(curSrc);
            alias_store<std::uint64_t>(cur_dest, w1);
            alias_store<std::uint64_t>(cur_dest, w2);
            alias_store<std::uint64_t>(cur_dest, w3);
            alias_store<std::uint64_t>(cur_dest, w4);
            alias_store<std::uint64_t>(cur_dest, w5);
            alias_store<std::uint64_t>(cur_dest, w6);
            alias_store<std::uint64_t>(cur_dest, w7);
            alias_store<std::uint64_t>(cur_dest, w8);
            n -= 8 * 8;
        }
        if (n >= 4 * 8)
        {
            auto w1 = alias_load<std::uint64_t>(curSrc);
            auto w2 = alias_load<std::uint64_t>(curSrc);
            auto w3 = alias_load<std::uint64_t>(curSrc);
            auto w4 = alias_load<std::uint64_t>(curSrc);
            alias_store<std::uint64_t>(cur_dest, w1);
            alias_store<std::uint64_t>(cur_dest, w2);
            alias_store<std::uint64_t>(cur_dest, w3);
            alias_store<std::uint64_t>(cur_dest, w4);
            n -= 4 * 8;
        }
        if (n >= 2 * 8)
        {
            auto w1 = alias_load<std::uint64_t>(curSrc);
            auto w2 = alias_load<std::uint64_t>(curSrc);
            alias_store<std::uint64_t>(cur_dest, w1);
            alias_store<std::uint64_t>(cur_dest, w2);
            n -= 2 * 8;
        }
        if (n >= 8)
        {
            auto w = alias_load<std::uint64_t>(curSrc);
            alias_store<std::uint64_t>(cur_dest, w);
            n -= 8;
        }
        if (n >= 4)
        {
            auto w = alias_load<std::uint32_t>(curSrc);
            alias_store<std::uint32_t>(cur_dest, w);
            n -= 4;
        }
        if (n >= 2)
        {
            auto w = alias_load<std::uint16_t>(curSrc);
            alias_store<std::uint16_t>(cur_dest, w);
            n -= 2;
        }
        if (n)
            *cur_dest = *curSrc;
        return dest;
    }

    void *memset(void *dest, int val, std::size_t n)
    {
        auto cur_dest = reinterpret_cast<std::uint8_t *>(dest);
        std::uint8_t byte = val;

        while (n && (reinterpret_cast<std::uintptr_t>(cur_dest) & 7))
        {
            *cur_dest++ = byte;
            --n;
        }

        auto pattern64 = static_cast<word<std::uint64_t>>(
            static_cast<std::uint64_t>(byte) | (static_cast<std::uint64_t>(byte) << 8)
            | (static_cast<std::uint64_t>(byte) << 16) | (static_cast<std::uint64_t>(byte) << 24)
            | (static_cast<std::uint64_t>(byte) << 32) | (static_cast<std::uint64_t>(byte) << 40)
            | (static_cast<std::uint64_t>(byte) << 48) | (static_cast<std::uint64_t>(byte) << 56)
        );

        auto pattern32 = static_cast<word<std::uint32_t>>(
            static_cast<std::uint32_t>(byte) | (static_cast<std::uint32_t>(byte) << 8)
            | (static_cast<std::uint32_t>(byte) << 16) | (static_cast<std::uint32_t>(byte) << 24)
        );

        auto pattern16 = static_cast<word<std::uint16_t>>(
            static_cast<std::uint16_t>(byte) | (static_cast<std::uint16_t>(byte) << 8)
        );

        while (n >= 8 * 8)
        {
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            n -= 8 * 8;
        }
        if (n >= 4 * 8)
        {
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            n -= 4 * 8;
        }
        if (n >= 2 * 8)
        {
            alias_store<std::uint64_t>(cur_dest, pattern64);
            alias_store<std::uint64_t>(cur_dest, pattern64);
            n -= 2 * 8;
        }
        if (n >= 8)
        {
            alias_store<std::uint64_t>(cur_dest, pattern64);
            n -= 8;
        }
        if (n >= 4)
        {
            alias_store<std::uint32_t>(cur_dest, pattern32);
            n -= 4;
        }
        if (n >= 2)
        {
            alias_store<std::uint16_t>(cur_dest, pattern16);
            n -= 2;
        }
        if (n)
            *cur_dest = byte;
        return dest;
    }

    [[gnu::weak]] void *memmove(void *dest, const void *src, std::size_t len)
    {
        auto pdest = static_cast<std::uint8_t *>(dest);
        auto psrc = static_cast<const std::uint8_t *>(src);

        if (src > dest)
        {
            for (std::size_t i = 0; i < len; i++)
                pdest[i] = psrc[i];
        }
        else if (src < dest)
        {
            for (std::size_t i = len; i > 0; i--)
                pdest[i - 1] = psrc[i - 1];
        }

        return dest;
    }

    [[gnu::weak]] int memcmp(const void *ptr1, const void *ptr2, std::size_t len)
    {
        auto p1 = static_cast<const std::uint8_t *>(ptr1);
        auto p2 = static_cast<const std::uint8_t *>(ptr2);

        for (std::size_t i = 0; i < len; i++)
        {
            if (p1[i] != p2[i])
                return p1[i] < p2[i] ? -1 : 1;
        }

        return 0;
    }

    [[gnu::weak]] void *memchr(const void *ptr, int ch, std::size_t len)
    {
        auto psrc = static_cast<const std::uint8_t *>(ptr);

        while (len-- > 0)
        {
            if (*psrc == ch)
                return const_cast<std::uint8_t *>(psrc);
            psrc++;
        }

        return nullptr;
    }

    std::size_t strlen(const char *str)
    {
        std::size_t length = 0;
        while (str[length])
            length++;
        return length;
    }

    std::size_t strnlen(const char *str, std::size_t len)
    {
        std::size_t length = 0;
        while (length < len && str[length])
            length++;
        return length;
    }

    char *strdup(const char *str)
    {
        const std::size_t len = strlen(str) + 1;

        void *newstr = lib::alloc(len);
        if (newstr == nullptr)
            return nullptr;

        return static_cast<char *>(memcpy(newstr, str, len));
    }

    char *strcat(char *dest, const char *src)
    {
        char *ptr = dest + strlen(dest);
        while (*src != '\0')
            *ptr++ = *src++;

        *ptr = '\0';
        return dest;
    }

    char *strncat(char *dest, const char *src, std::size_t len)
    {
        char* ptr = dest + strlen(dest);
        while (*src != '\0' && len--)
            *ptr++ = *src++;
        *ptr = '\0';
        return dest;
    }

    char *strchr(const char *str, int ch)
    {
        while (*str && *str != ch)
            str++;
        return const_cast<char *>(ch == *str ? str : nullptr);
    }

    int strcmp(const char *str1, const char *str2)
    {
        while (*str1 && *str2 && *str1 == *str2)
        {
            str1++;
            str2++;
        }
        return *str1 - *str2;
    }

    int strncmp(const char *str1, const char *str2, std::size_t len)
    {
        while (*str1 && *str2 && *str1 == *str2 && len--)
        {
            str1++;
            str2++;
        }
        if (len == 0)
            return 0;

        return *str1 - *str2;
    }

    char *strcpy(char *dest, const char *src)
    {
        char *ptr = dest;
        while (*src != '\0')
        {
            *dest = *src;
            dest++;
            src++;
        }
        *dest = '\0';
        return ptr;
    }

    char *strncpy(char *dest, const char *src, std::size_t len)
    {
        char *ptr = dest;
        while (*src != '\0' && len--)
        {
            *dest = *src;
            dest++;
            src++;
        }
        *dest = '\0';
        return ptr;
    }

    char *strstr(const char *str, const char *substr)
    {
        const char *a = str, *b = substr;
        while (true)
        {
            if (*b == 0)
                return const_cast<char *>(str);

            if (*a == 0)
                return nullptr;

            if (*a++ != *b++)
            {
                a = ++str;
                b = substr;
            }
        }
    }

    void strrev(char *str)
    {
        auto len = strlen(reinterpret_cast<const char *>(str));

        for (std::size_t i = 0, j = len - 1; i < j; i++, j--)
        {
            const char a = str[i];
            str[i] = str[j];
            str[j] = a;
        }
    }
} // extern "C"