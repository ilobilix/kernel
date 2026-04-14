// Copyright (C) 2024-2025  ilobilo

export module lib:string;

import :errno;
import :types;
import fmt;
import std;

export namespace lib
{
    struct user_string
    {
        std::string str;
        explicit user_string(const char __user *ustr);
    };

    template<std::size_t N>
    struct comptime_string
    {
        char value[N];

        consteval comptime_string(const char (&str)[N])
        {
            std::copy_n(str, N, value);
        }

        consteval bool is_empty() const
        {
            return N <= 1;
        }

        consteval auto data() const
        {
            return value;
        }

        consteval std::size_t size() const
        {
            return N - 1;
        }
    };

    // from mlibc
    template<typename Ret>
    constexpr Ret str2int(const char *nptr, char **endptr, int _base)
    {
        using URet = std::make_unsigned_t<Ret>;

        auto base = static_cast<Ret>(_base);
        auto str = nptr;

        if (base < 0 || base == 1)
        {
            if (endptr != nullptr)
                *endptr = const_cast<char *>(nptr);
            return 0;
        }

        while (std::isspace(*str))
            str++;

        bool negative = false;
        if (*str == '-')
        {
            negative = true;
            str++;
        }
        else if (*str == '+')
            str++;

        bool octal = (str[0] == '0');
        bool hex = octal && (str[1] == 'x' || str[1] == 'X');

        if ((base == 0 || base == 16) && hex == true && std::isxdigit(str[2]))
        {
            str += 2;
            base = 16;
        }
        else if ((base == 0 || base == 8) && octal == true)
            base = 8;
        else if (base == 0)
            base = 10;

        URet cutoff = 0;
        URet cutlim = 0;
        if constexpr (std::is_unsigned_v<Ret>)
        {
            cutoff = std::numeric_limits<Ret>::max() / base;
            cutoff = std::numeric_limits<Ret>::max() % base;
        }
        else
        {
            Ret co = negative ? std::numeric_limits<Ret>::min() : std::numeric_limits<Ret>::max();
            cutlim = negative ? -(co % base) : co % base;
            co /= negative ? -base : base;
            cutoff = co;
        }

        URet total = 0;
        bool converted = false;
        bool out_of_range = false;

        for (auto c = *str; c != '\0'; c = *++str)
        {
            URet digit = 0;
            if (std::isdigit(c))
                digit = c - '0';
            else if (std::isupper(c))
                digit = c - 'A' + 0x10;
            else if (std::islower(c))
                digit = c - 'a' + 0x10;
            else
                break;

            if (digit >= static_cast<URet>(base))
                break;

            if (out_of_range)
                continue;

            if (total >= cutoff || (total == cutoff && digit > cutlim))
                out_of_range = true;
            else
            {
                total = (total * base) + digit;
                converted = true;
            }
        }

        if (endptr != nullptr)
            *endptr = const_cast<char *>(converted ? str : nptr);

        if (out_of_range)
        {
            errno = ERANGE;
            if constexpr (std::is_unsigned_v<Ret>)
                return std::numeric_limits<Ret>::max();
            else
                return negative ? std::numeric_limits<Ret>::min() : std::numeric_limits<Ret>::max();
        }

        return negative ? -total : total;
    }

    template<typename Type>
    constexpr Type oct2int(std::string_view str)
    {
        Type value = 0;
        auto ptr = str.data();
        auto len = str.length();

        while (ptr < str.end() && *ptr && len > 0)
        {
            value = value * 8 + (*ptr++ - '0');
            len--;
        }
        return value;
    }
} // export namespace lib

template<>
struct fmt::formatter<lib::user_string> : fmt::formatter<std::string>
{
    template<typename FormatContext>
    auto format(lib::user_string str, FormatContext &ctx) const
    {
        return formatter<std::string>::format(str.str.empty() ? std::string { "(null)" } : str.str, ctx);
    }
};