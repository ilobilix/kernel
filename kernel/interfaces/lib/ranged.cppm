// Copyright (C) 2024-2025  ilobilo

export module lib:ranged;

import :panic;
import std;

export namespace lib
{
    template<std::integral Type, Type Min, Type Max>
    class ranged
    {
        public:
        static inline constexpr Type min = Min;
        static inline constexpr Type max = Max;

        private:
        Type _value;

        constexpr void set_value(Type val)
        {
            if (val < min || max < val) [[unlikely]]
                lib::panic("ranged: value {} outside of range [{}, {}]", val, min, max);
            _value = val;
        }

        public:
        constexpr ranged(Type val = 0) { set_value(val); }

        constexpr ranged(const ranged &other) : _value { other._value } { }
        constexpr ranged(ranged &&other) : _value { std::move(other._value) } { }

        constexpr ranged &operator=(Type rhs)
        {
            set_value(rhs);
            return *this;
        }

        constexpr ranged &operator=(const ranged &rhs)
        {
            _value = rhs._value;
            return *this;
        }

        constexpr ranged &operator=(ranged &&rhs)
        {
            _value = std::move(rhs._value);
            return *this;
        }

        constexpr ranged &operator++()
        {
            if (_value < max)
                _value++;
            return *this;
        }

        constexpr ranged &operator++(int)
        {
            ranged tmp = *this;
            if (_value < max)
                _value++;
            return tmp;
        }

        constexpr ranged &operator--()
        {
            if (_value > min)
                _value--;
            return *this;
        }

        constexpr ranged &operator--(int)
        {
            ranged tmp = *this;
            if (_value > min)
                _value--;
            return tmp;
        }

        constexpr operator Type() const { return _value; }
        Type value() const { return _value; }

        Type set_min() { _value = Min; }
        Type set_max() { _value = Max; }
    };
} // export namespace lib