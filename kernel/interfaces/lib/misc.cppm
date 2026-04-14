// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>

export module lib:misc;
import std;

export namespace lib
{
    // should get optimised out in release mode
    inline constexpr void unused([[maybe_unused]] auto &&...) { }

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

    template<typename Type>
    struct chain_wrapper
    {
        private:
        union {
            chain_wrapper *_other;
            Type _val;
        };
        bool _chained;

        public:
        constexpr chain_wrapper() : _val { }, _chained { false } { }
        constexpr chain_wrapper(Type val) : _val { val }, _chained { false } { }

        constexpr chain_wrapper(const chain_wrapper &other)
            : _other { const_cast<chain_wrapper *>(std::addressof(other)) }, _chained(true) { }

        constexpr chain_wrapper(chain_wrapper &&other) = delete;

        constexpr chain_wrapper &operator=(Type val)
        {
            _val = val;
            _chained = false;
            return *this;
        }

        constexpr chain_wrapper &operator=(const chain_wrapper &other)
        {
            _other = const_cast<chain_wrapper *>(std::addressof(other));
            _chained = true;
            return *this;
        }

        constexpr Type &get()
        {
            if (_chained == true)
                return _other->get();
            return _val;
        }

        constexpr Type *operator->()
        {
            return std::addressof(get());
        }

        template<typename Type1>
        constexpr auto operator[](Type1 &&index) -> decltype(auto)
        {
            return get()[std::forward<Type1>(index)];
        }

        constexpr bool is_chained() const
        {
            return _chained;
        }

        operator Type() { return get(); }
    };
} // export namespace lib