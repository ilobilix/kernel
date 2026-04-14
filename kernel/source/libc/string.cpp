// Copyright (C) 2024  ilobilo

import std;

extern "C"
{
    [[gnu::weak]] void *memcpy(void *dest, const void *src, std::size_t len)
    {
        auto pdest = static_cast<std::uint8_t *>(dest);
        auto psrc = static_cast<const std::uint8_t *>(src);

        for (std::size_t i = 0; i < len; i++)
            pdest[i] = psrc[i];

        return dest;
    }

    [[gnu::weak]] void *memset(void *dest, int ch, std::size_t len)
    {
        auto ptr = static_cast<std::uint8_t *>(dest);

        for (std::size_t i = 0; i < len; i++)
            ptr[i] = static_cast<std::uint8_t>(ch);

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
        std::size_t len = strlen(str) + 1;

        void *newstr = std::malloc(len);
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
            char a = str[i];
            str[i] = str[j];
            str[j] = a;
        }
    }
} // extern "C"