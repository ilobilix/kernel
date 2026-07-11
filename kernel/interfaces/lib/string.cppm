// Copyright (C) 2024-2026  ilobilo

export module lib:string;

import :types;
import fmt;
import std;

export namespace lib
{
    struct user_string
    {
        std::string str;

        static std::optional<std::string> get(
            const char __user *ustr, std::size_t max_length = 4096
        );

        explicit user_string(const char __user *ustr)
        {
            auto ret = get(ustr);
            if (ret.has_value())
                str = std::move(*ret);
            else
                str = "";
        }
    };

    template<std::size_t Len>
    struct comptime_string
    {
        char value[Len];

        consteval comptime_string() = default;

        consteval comptime_string(const char (&str)[Len])
        {
            std::copy_n(str, Len, value);
        }

        consteval comptime_string(std::array<char, Len> data)
        {
            std::copy_n(data.data(), Len, value);
        }

        template<typename = void>
        consteval comptime_string(const char *str)
        {
            std::copy_n(str, Len, value);
        }

        consteval bool is_empty() const
        {
            return Len <= 1;
        }

        consteval auto data() const
        {
            return value;
        }

        consteval std::size_t size() const
        {
            return Len - 1;
        }

        constexpr operator std::string_view() const
        {
            return std::string_view { value, size() };
        }
    };

    template<auto Fmt, auto ...Args>
    consteval auto consteval_format()
    {
        constexpr auto size = fmt::formatted_size(Fmt, Args...);
        std::array<char, size + 1> buffer { };
        fmt::format_to(buffer.begin(), Fmt, Args...);
        return comptime_string { buffer };
    }

    constexpr std::string_view trim(std::string_view str)
    {
        const auto check = [](char chr) {
            return chr == ' ' || chr == '\t' || chr == '\n' || chr == '\r';
        };
        while (!str.empty() && check(str.front()))
            str.remove_prefix(1);
        while (!str.empty() && check(str.back()))
            str.remove_suffix(1);
        return str;
    }

    // from mlibc
    template<typename Ret>
    constexpr std::optional<Ret> str2int(const char *nptr, char **endptr, int _base)
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

        const auto isspace = [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
        };
        while (isspace(*str))
            str++;

        bool negative = false;
        if (*str == '-')
        {
            negative = true;
            str++;
        }
        else if (*str == '+')
            str++;

        const bool octal = (str[0] == '0');
        const bool hex = octal && (str[1] == 'x' || str[1] == 'X');
        const bool bin = octal && (str[1] == 'b' || str[1] == 'B');

        const auto isxdigit = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        };
        if ((base == 0 || base == 16) && hex && isxdigit(str[2]))
        {
            str += 2;
            base = 16;
        }
        else if ((base == 0 || base == 2) && bin)
        {
            str += 2;
            base = 2;
        }
        else if ((base == 0 || base == 8) && octal)
            base = 8;
        else if (base == 0)
            base = 10;

        URet cutoff = 0;
        URet cutlim = 0;
        if constexpr (std::is_unsigned_v<Ret>)
        {
            cutoff = std::numeric_limits<Ret>::max() / base;
            cutlim = std::numeric_limits<Ret>::max() % base;
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
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'A' && c <= 'Z')
                digit = c - 'A' + 10;
            else if (c >= 'a' && c <= 'z')
                digit = c - 'a' + 10;
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
            return std::nullopt;

        return negative ? -total : total;
    }

    template<typename Type>
    constexpr Type oct2int(std::span<char> data)
    {
        Type value = 0;
        auto ptr = data.data();
        auto len = data.size_bytes();

        const auto end = ptr + len;
        while (ptr < end && *ptr && len > 0)
        {
            value = value * 8 + (*ptr++ - '0');
            len--;
        }
        return value;
    }
} // export namespace lib

export
{
    template<>
    struct fmt::formatter<lib::user_string> : fmt::formatter<std::string>
    {
        template<typename FormatContext>
        auto format(lib::user_string str, FormatContext &ctx) const
        {
            return formatter<std::string>::format(str.str.empty() ? "(null)"s : str.str, ctx);
        }
    };

    template<std::size_t Len>
    struct fmt::formatter<lib::comptime_string<Len>> : fmt::formatter<std::string_view>
    {
        template<typename FormatContext>
        constexpr auto format(lib::comptime_string<Len> str, FormatContext &ctx) const
        {
            return formatter<std::string_view>::format(str, ctx);
        }
    };

    template<lib::comptime_string Str>
    constexpr auto operator""_cs() { return Str; }
} // export
