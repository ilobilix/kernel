// Copyright (C) 2024-2026  ilobilo

export module lib:kvargs;

import :unused;
import :string;
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
        constexpr kvarg_base(Type def) :  _value { def } { }

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
        constexpr kvarg(int base) : _base { base } { }
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

    template<typename Type>
    concept is_kvarg = requires {
        { Type::key } -> std::convertible_to<std::string_view>;
    };

    template<comptime_string Key, is_kvarg ...Args>
    consteval std::optional<std::size_t> index_of_key()
    {
        std::size_t idx = 0;
        std::size_t found_at = sizeof...(Args);
        ((Args::key == std::string_view { Key.value } ? (found_at = idx, idx++) : idx++), ...);
        if (found_at == sizeof...(Args))
            return std::nullopt;
        return found_at;
    }

    template<std::invocable<std::string_view, std::string_view> Func>
    constexpr void kvparse(std::string_view input, char separator, Func &&func)
    {
        std::size_t pos = 0;
        while (pos < input.size())
        {
            pos = input.find_first_not_of(separator, pos);
            if (pos == input.npos)
                break;

            const auto end = [&] {
                for (auto i = pos; i < input.size(); i++)
                {
                    const auto chr = input[i];
                    if (chr == '"' || chr == '\'')
                    {
                        const auto close = input.find(chr, i + 1);
                        if (close == input.npos)
                            return input.size();
                        i = close;
                    }
                    else if (chr == separator)
                        return i;
                }
                return input.size();
            } ();

            const auto pair = input.substr(pos, end - pos);
            const auto eq = pair.find('=');

            if (const auto key = pair.substr(0, eq); !key.empty())
            {
                auto value = (eq == pair.npos) ? ""sv : pair.substr(eq + 1);
                if (value.size() >= 2 &&
                    ((value.front() == '"' && value.back() == '"') ||
                    (value.front() == '\'' && value.back() == '\'')))
                    value = value.substr(1, value.size() - 2);
                func(key, value);
            }

            pos = end + 1;
        }
    }

    template<is_kvarg ...Args>
    class kvargs
    {
        private:
        std::tuple<Args...> _args;

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
            constexpr auto idx = index_of_key<Key, Args...>();
            static_assert(idx.has_value(), "kvargs: key not found");
            return std::get<*idx>(_args);
        }
    };
} // export namespace lib
