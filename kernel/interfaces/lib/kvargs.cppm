// Copyright (C) 2024-2026  ilobilo

export module lib:kvargs;

import :unused;
import :string;
import :math;
import std;

export namespace lib
{
    template<typename Type, comptime_string Key>
    class kvarg_base
    {
        protected:
        std::optional<Type> _value;

        public:
        static constexpr std::string_view key { Key.value };

        constexpr kvarg_base() : _value { std::nullopt } { }
        constexpr kvarg_base(Type def) : _value { def } { }

        constexpr bool parse(std::string_view input)
        {
            unused(input);
            return false;
        }

        constexpr bool has_value() const
        {
            return _value.has_value();
        }

        constexpr const Type &value() const
        {
            return *_value;
        }
    };

    template<typename Type, comptime_string Key>
    class kvarg;

    template<std::integral Type, comptime_string Key>
    class kvarg<Type, Key> : public kvarg_base<Type, Key>
    {
        private:
        int _base;

        public:
        constexpr kvarg(int base = 0) : _base { base } { }
        constexpr kvarg(int base, Type def)
            : kvarg_base<Type, Key> { def }, _base { base } { }

        constexpr bool parse(std::string_view input)
        {
            char *end;
            const auto res = str2int<Type>(input.data(), &end, _base);
            if (!res.has_value() || end != input.data() + input.size())
                return false;

            this->_value = *res;
            return true;
        }
    };

    template<comptime_string Key>
    class kvarg<std::string_view, Key> : public kvarg_base<std::string_view, Key>
    {
        public:
        using kvarg_base<std::string_view, Key>::kvarg_base;

        constexpr bool parse(std::string_view input)
        {
            this->_value = input;
            return true;
        }
    };

    template<comptime_string Key>
    class kvarg<bool, Key> : public kvarg_base<bool, Key>
    {
        public:
        using kvarg_base<bool, Key>::kvarg_base;

        constexpr bool parse(std::string_view input)
        {
            if (input.empty() || input == "true" || input == "TRUE" || input == "1")
                this->_value = true;
            else if (input == "false" || input == "FALSE" || input == "0")
                this->_value = false;
            else
                return false;
            return true;
        }
    };

    template<std::integral Type, comptime_string Key, bool Percent>
    class kvarg_size : public kvarg_base<Type, Key>
    {
        private:
        int _base;
        [[no_unique_address]]
        std::conditional_t<Percent, Type, std::monostate> _pmax { };

        public:
        constexpr kvarg_size(int base = 0) requires (!Percent)
            : _base { base } { }
        constexpr kvarg_size(int base, Type def) requires (!Percent)
            : kvarg_base<Type, Key> { def }, _base { base } { }

        constexpr kvarg_size(Type pmax, int base = 0) requires Percent
            : _base { base }, _pmax { pmax } { }
        constexpr kvarg_size(Type pmax, int base, Type def) requires Percent
            : kvarg_base<Type, Key> { def }, _base { base }, _pmax { pmax } { }

        constexpr bool parse(std::string_view input)
        {
            if (input.empty())
                return false;

            char *end;
            const auto res = str2int<Type>(input.data(), &end, _base);
            if (!res.has_value())
                return false;

            const auto consumed = static_cast<std::size_t>(end - input.data());
            if (consumed == 0)
                return false;

            Type mult = 1;
            const auto rest = input.substr(consumed);
            if (rest.size() == 1)
            {
                switch (rest[0])
                {
                    case 'k':
                    case 'K':
                        mult = kib(1);
                        break;
                    case 'm':
                    case 'M':
                        mult = mib(1);
                        break;
                    case 'g':
                    case 'G':
                        mult = gib(1);
                        break;
                    case '%':
                        if constexpr (Percent)
                        {
                            if (*res > 100)
                                return false;

                            if constexpr (std::is_signed_v<Type>)
                            {
                                if (*res < 0)
                                    return false;
                            }

                            this->_value = (*res * _pmax) / 100;
                            return true;
                        }
                        return false;
                    default:
                        return false;
                }
            }
            else if (!rest.empty())
                return false;

            this->_value = *res * mult;
            return true;
        }
    };

    template <typename Type>
    concept is_kvarg = requires(Type kv, std::string_view sv) {
        { Type::key } -> std::convertible_to<std::string_view>;
        { kv.parse(sv) } -> std::convertible_to<bool>;
    };

    constexpr bool kvparse_next(
        std::string_view input, char separator,
        std::size_t &pos, std::pair<std::string_view, std::string_view> &out)
    {
        while (true)
        {
            if (pos >= input.size())
                return false;

            pos = input.find_first_not_of(separator, pos);
            if (pos == input.npos)
                return false;

            std::size_t end = pos;
            for (; end < input.size(); end++)
            {
                const auto chr = input[end];
                if (chr == '"' || chr == '\'')
                {
                    const auto close = input.find(chr, end + 1);
                    if (close == input.npos)
                    {
                        end = input.size();
                        break;
                    }
                    end = close;
                }
                else if (chr == separator)
                    break;
            }

            const auto pair = input.substr(pos, end - pos);
            const auto eq = pair.find('=');
            const auto key = pair.substr(0, eq);

            pos = (end < input.size()) ? end + 1 : input.size();

            if (key.empty())
                continue;

            auto value = (eq == pair.npos) ? std::string_view { } : pair.substr(eq + 1);

            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\'')))
                value = value.substr(1, value.size() - 2);

            out = { key, value };
            return true;
        }
    }

    template<std::invocable<std::string_view, std::string_view> Func>
    constexpr void kvparse(std::string_view input, char separator, Func &&func)
    {
        std::size_t pos = 0;
        std::pair<std::string_view, std::string_view> out;
        while (kvparse_next(input, separator, pos, out))
            func(out.first, out.second);
    }

    class kvparse_view : public std::ranges::view_interface<kvparse_view>
    {
        private:
        std::string_view _input;
        char _separator;

        public:
        class iterator
        {
            std::string_view _input;
            char _separator;
            std::size_t _pos;
            std::pair<std::string_view, std::string_view> _current;

            constexpr void advance()
            {
                if (!kvparse_next(_input, _separator, _pos, _current))
                    _pos = std::string_view::npos;
            }

            public:
            using value_type = std::pair<std::string_view, std::string_view>;
            using difference_type = std::ptrdiff_t;
            using iterator_concept = std::input_iterator_tag;

            constexpr iterator() = default;
            constexpr iterator(std::string_view input, char separator)
                : _input { input }, _separator { separator }, _pos { 0 }
            { advance(); }

            constexpr const value_type &operator*() const { return _current; }

            constexpr iterator &operator++()
            {
                advance();
                return *this;
            }

            constexpr void operator++(int) { advance(); }

            constexpr bool operator==(std::default_sentinel_t) const
            {
                return _pos == std::string_view::npos;
            }
        };

        constexpr kvparse_view() = default;
        constexpr kvparse_view(std::string_view input, char separator)
            : _input { input }, _separator { separator } { }

        constexpr iterator begin() const { return iterator { _input, _separator }; }
        constexpr std::default_sentinel_t end() const { return { }; }
    };
    static_assert(std::ranges::input_range<kvparse_view>);

    template<is_kvarg ...Args>
    class kvargs
    {
        private:
        std::tuple<Args...> _args;

        template<comptime_string Key>
        static consteval std::optional<std::size_t> index_of_key()
        {
            std::size_t idx = 0;
            std::size_t found_at = sizeof...(Args);
            ((Args::key == std::string_view { Key.value } ? (found_at = idx, idx++) : idx++), ...);
            if (found_at == sizeof...(Args))
                return std::nullopt;
            return found_at;
        }

        public:
        constexpr kvargs(Args ...args)
            : _args { std::move(args)... } { }

        constexpr void parse(std::string_view input, char separator)
        {
            kvparse(input, separator, [this](std::string_view key, std::string_view value) {
                std::apply([&](auto &...arg) {
                    ((arg.key == key && arg.parse(value)) || ...);
                }, _args);
            });
        }

        template<comptime_string Key>
        constexpr auto &get()
        {
            constexpr auto idx = index_of_key<Key>();
            static_assert(idx.has_value(), "kvargs: key not found");
            return std::get<*idx>(_args);
        }
    };
} // export namespace lib
