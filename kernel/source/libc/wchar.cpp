// Copyright (C) 2024-2025  ilobilo

import std;

extern "C"
{
    wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, std::size_t count)
    {
        return static_cast<wchar_t *>(std::memcpy(dest, src, count * sizeof(wchar_t)));
    }

    wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, std::size_t count)
    {
        return static_cast<wchar_t *>(std::memmove(dest, src, count * sizeof(wchar_t)));
    }

    wchar_t *wmemset(wchar_t *dest, wchar_t ch, std::size_t count)
    {
        auto ret = dest;
        while (count--)
            *dest++ = ch;
        return ret;
    }

    wchar_t *wmemchr(const wchar_t *ptr, wchar_t ch, std::size_t count)
    {
        while (count != 0 && *ptr != ch)
        {
            count--;
            ptr++;
        }
        return count ? const_cast<wchar_t *>(ptr) : nullptr;
    }

    int wmemcmp(const wchar_t *lhs, const wchar_t *rhs, std::size_t count)
    {
        while (count != 0 && *lhs == *rhs)
        {
            count--;
            lhs++;
            rhs++;
        }
        return count ? *lhs - *rhs : 0;
    }

    std::size_t wcslen(const wchar_t *start)
    {
        const wchar_t *end = start;
        while (*end != L'\0')
            end++;
        return end - start;
    }
} // extern "C"