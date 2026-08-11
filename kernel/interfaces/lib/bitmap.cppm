// Copyright (C) 2024-2026  ilobilo

export module lib:bitmap;

import :bug_on;
import :math;
import std;

export namespace lib
{
    class bitmap_view
    {
        private:
        std::uint8_t *_data;
        std::size_t _count;

        public:
        struct bit;

        constexpr bitmap_view() : _data { nullptr }, _count { 0 } { }
        constexpr bitmap_view(std::uint8_t *data, std::size_t count)
            : _data { data }, _count { count } { }

        constexpr void clear(int ch = 0)
        {
            lib::bug_on(_data == nullptr);
            std::memset(_data, ch, div_roundup(_count, 8u));
        }

        constexpr bool get(std::size_t index) const
        {
            lib::bug_on(_data == nullptr);
            return _data[index / 8] & (1 << (index % 8));
        }

        constexpr bool set(std::size_t index, bool value)
        {
            lib::bug_on(_data == nullptr);
            const auto ret = get(index);

            if (value == true)
                _data[index / 8] |= (1 << (index % 8));
            else
                _data[index / 8] &= ~(1 << (index % 8));

            return ret;
        }

        constexpr bit operator[](std::size_t index);
        constexpr bool operator[](std::size_t index) const { return get(index); }

        constexpr std::size_t length() const { return _count; }
        constexpr std::size_t size() const { return _count; }
        constexpr std::size_t size_bytes() const { return div_roundup(_count, 8u); }

        constexpr bool empty() const
        {
            for (std::size_t i = 0; i < div_roundup(_count, 8u); i++)
            {
                if (_data[i] != 0)
                    return false;
            }
            return true;
        }

        constexpr std::size_t count() const
        {
            lib::bug_on(_data == nullptr);

            const auto bytes = div_roundup(_count, 8u);
            if (bytes == 0)
                return 0;

            std::size_t total = 0;
            for (std::size_t i = 0; i + 1 < bytes; i++)
                total += std::popcount(_data[i]);

            const auto rest = _count % 8;
            const std::uint8_t mask = rest == 0 ? 0xFF : (1u << rest) - 1;
            return total + std::popcount(static_cast<std::uint8_t>(_data[bytes - 1] & mask));
        }

        constexpr std::uint8_t *data()
        {
            lib::bug_on(_data == nullptr);
            return _data;
        }

        constexpr const std::uint8_t *data() const
        {
            lib::bug_on(_data == nullptr);
            return _data;
        }
    };

    struct bitmap_view::bit
    {
        bitmap_view view;
        std::size_t index;

        constexpr bit &operator=(bool value)
        {
            view.set(index, value);
            return *this;
        }

        constexpr operator bool() const { return view.get(index); }
    };

    constexpr bitmap_view::bit bitmap_view::operator[](std::size_t index)
    {
        lib::bug_on(_data == nullptr);
        return bit { *this, index };
    }

    class bitmap
    {
        private:
        std::uint8_t *_data;
        std::size_t _count;
        bool _allocated;

        public:
        friend constexpr void swap(bitmap &lhs, bitmap &rhs)
        {
            using std::swap;
            swap(lhs._data, rhs._data);
            swap(lhs._count, rhs._count);
            swap(lhs._allocated, rhs._allocated);
        }

        constexpr bitmap() : _data { nullptr }, _count { 0 }, _allocated { false } { }
        constexpr bitmap(std::uint8_t *data, std::size_t count)
            : _data { data }, _count { count }, _allocated { false } { }

        bitmap(std::size_t count)
            : _data { new std::uint8_t[div_roundup(count, 8u)]() },
              _count { count }, _allocated { true } { }

        bitmap(const bitmap &other) : bitmap { other._count }
        {
            std::memcpy(_data, other._data, size_bytes());
        }

        bitmap &operator=(const bitmap &other)
        {
            if (this != &other)
            {
                bitmap copy { other };
                swap(*this, copy);
            }
            return *this;
        }

        constexpr bitmap(bitmap &&other) : bitmap { }
        {
            swap(*this, other);
        }

        constexpr bitmap &operator=(bitmap &&other)
        {
            if (this != &other)
                swap(*this, other);
            return *this;
        }

        constexpr ~bitmap()
        {
            if (_allocated)
                delete[] _data;
        }

        constexpr operator bitmap_view() const { return view(); }
        constexpr bitmap_view view() const { return { _data, _count }; }

        constexpr void initialise(std::uint8_t *data, std::size_t count)
        {
            lib::bug_on(_data != nullptr);
            _data = data;
            _count = count;
        }

        void initialise(std::size_t count)
        {
            lib::bug_on(_data != nullptr);
            _data = new std::uint8_t[div_roundup(count, 8u)]();
            _count = count;
            _allocated = true;
        }

        constexpr void clear(int ch = 0) { view().clear(ch); }
        constexpr bool get(std::size_t index) const { return view().get(index); }
        constexpr bool set(std::size_t index, bool value) { return view().set(index, value); }
        constexpr auto operator[](std::size_t index) { return view()[index]; }

        constexpr std::size_t length() const { return _count; }
        constexpr std::size_t size() const { return _count; }
        constexpr std::size_t size_bytes() const { return div_roundup(_count, 8u); }
        constexpr bool empty() const { return view().empty(); }

        constexpr std::uint8_t *data() { return view().data(); }
        constexpr const std::uint8_t *data() const { return view().data(); }
    };

    template<std::size_t NBits>  requires (NBits != 0)
    class static_bitmap
    {
        private:
        std::uint8_t _storage[div_roundup(NBits, 8u)] { };

        public:
        constexpr operator bitmap_view() const { return view(); }

        constexpr bitmap_view view() const
        {
            return { const_cast<std::uint8_t *>(_storage), NBits };
        }

        constexpr void clear(int ch = 0) { view().clear(ch); }
        constexpr bool get(std::size_t index) const { return view().get(index); }
        constexpr bool set(std::size_t index, bool value) { return view().set(index, value); }
        constexpr auto operator[](std::size_t index) { return view()[index]; }

        constexpr std::size_t length() const { return NBits; }
        constexpr std::size_t size() const { return NBits; }
        constexpr std::size_t size_bytes() const { return sizeof(_storage); }
        constexpr bool empty() const { return view().empty(); }

        constexpr std::uint8_t *data() { return _storage; }
        constexpr const std::uint8_t *data() const { return _storage; }
    };
} // export namespace lib
