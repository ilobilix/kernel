// Copyright (C) 2024-2025  ilobilo

export module lib:bitmap;

import :bug_on;
import :math;
import std;

export namespace lib
{
    class bitmap
    {
        private:
        std::uint8_t *_data;
        std::size_t _count;

        bool _initialised;
        bool _allocated;

        public:
        friend constexpr void swap(bitmap &lhs, bitmap &rhs)
        {
            using std::swap;
            swap(lhs._data, rhs._data);
            swap(lhs._count, rhs._count);
            swap(lhs._initialised, rhs._initialised);
            swap(lhs._allocated, rhs._allocated);
        }

        constexpr bitmap()
            : _data { nullptr }, _count { 0 }, _initialised { false }, _allocated { false } { };
        constexpr bitmap(std::uint8_t *data, std::size_t count)
            : _data { data }, _count { count }, _initialised { true }, _allocated { false } { };

        constexpr bitmap(std::size_t count)
        {
            const auto size = div_roundup(count, 8u);

            _data = new std::uint8_t[size]();
            _count = count;
            _allocated = true;

            _initialised = true;

            clear();
        }

        bitmap(const bitmap &other) = delete;
        bitmap &operator=(const bitmap &other) = delete;

        constexpr bitmap(bitmap &&other) : bitmap { } { swap(*this, other); }
        constexpr bitmap &operator=(bitmap &&other) { swap(*this, other); return *this; }

        constexpr ~bitmap()
        {
            if (_allocated)
                delete[] _data;
        }

        constexpr void initialise(std::uint8_t *data, std::size_t count)
        {
            lib::bug_on(!!_initialised);
            _data = data;
            _count = count;
            _initialised = true;
        }

        constexpr void clear(int ch = 0)
        {
            lib::bug_on(!_initialised);
            std::memset(_data, ch, div_roundup(_count, 8u));
        }

        struct bit
        {
            bitmap &parent;
            std::size_t index;

            constexpr bit(bitmap &parent, std::size_t index)
                : parent(parent), index(index) { }

            constexpr void operator=(bool value)
            {
                parent.set(index, value);
            }

            constexpr operator bool() const
            {
                return parent.get(index);
            }
        };

        constexpr bit operator[](std::size_t index)
        {
            lib::bug_on(!_initialised);
            return bit(*this, index);
        }

        constexpr bool get(std::size_t index)
        {
            lib::bug_on(!_initialised);
            return _data[index / 8] & (1 << (index % 8));
        }

        constexpr bool set(std::size_t index, bool value)
        {
            lib::bug_on(!_initialised);
            const auto ret = get(index);

            if (value == true)
                _data[index / 8] |= (1 << (index % 8));
            else
                _data[index / 8] &= ~(1 << (index % 8));

            return ret;
        }

        constexpr std::size_t length() const
        {
            return _count;
        }
    };
} // export namespace lib