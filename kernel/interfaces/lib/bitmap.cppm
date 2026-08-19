// Copyright (C) 2024-2026  ilobilo

export module lib:bitmap;

import :bug_on;
import :math;
import std;

export namespace lib
{
    template<bool Atomic, bool Const = false, typename Word = std::uint8_t>
        requires (std::unsigned_integral<Word> && !(Atomic && Const))
    class bitmap_view_base
    {
        template<bool OtherAtomic, bool OtherConst, typename OtherWord>
            requires (std::unsigned_integral<OtherWord> && !(OtherAtomic && OtherConst))
        friend class bitmap_view_base;

        private:
        static constexpr std::size_t word_bits = sizeof(Word) * 8;
        using word_t = std::conditional_t<Const, const Word, Word>;

        word_t *_data;
        std::size_t _count;

        static constexpr Word get_bit(std::size_t index)
        {
            return static_cast<Word>(1) << (index % word_bits);
        }

        constexpr Word valid_bits(std::size_t idx) const
        {
            const auto rest = _count % word_bits;
            if (rest == 0 || idx + 1 != size_words())
                return ~static_cast<Word>(0);
            return (static_cast<Word>(1) << rest) - 1;
        }

        constexpr Word load(std::size_t idx, std::memory_order order) const
        {
            if constexpr (Atomic)
            {
                if (!std::is_constant_evaluated())
                    return std::atomic_ref { _data[idx] } .load(order);
            }
            return _data[idx];
        }

        constexpr void store(std::size_t idx, Word value, std::memory_order order)
            requires (!Const)
        {
            if constexpr (Atomic)
            {
                if (!std::is_constant_evaluated())
                {
                    std::atomic_ref { _data[idx] } .store(value, order);
                    return;
                }
            }
            _data[idx] = value;
        }

        public:
        struct bit;

        constexpr bitmap_view_base() : _data { nullptr }, _count { 0 } { }
        constexpr bitmap_view_base(word_t *data, std::size_t count)
            : _data { data }, _count { count } { }

        template<bool OtherConst> requires (Const && !OtherConst)
        constexpr bitmap_view_base(const bitmap_view_base<Atomic, OtherConst, Word> &other)
            : _data { other._data }, _count { other._count } { }

        template<bool OtherAtomic> requires (OtherAtomic != Atomic)
        explicit constexpr bitmap_view_base(const bitmap_view_base<OtherAtomic, Const, Word> &other)
            : _data { other._data }, _count { other._count } { }

        constexpr void clear(int ch = 0, std::memory_order order = std::memory_order_seq_cst)
            requires (!Const)
        {
            lib::bug_on(_data == nullptr);

            if constexpr (!Atomic)
            {
                if (!std::is_constant_evaluated())
                {
                    std::memset(_data, ch, size_bytes());
                    return;
                }
            }

            Word fill = 0;
            for (std::size_t i = 0; i < sizeof(Word); i++)
                fill |= static_cast<Word>(ch) << (i * 8);

            for (std::size_t i = 0; i < size_words(); i++)
                store(i, fill, order);
        }

        constexpr bool get(std::size_t index, std::memory_order order = std::memory_order_seq_cst) const
        {
            lib::bug_on(_data == nullptr);
            return load(index / word_bits, order) & get_bit(index);
        }

        constexpr bool set(std::size_t index, bool value, std::memory_order order = std::memory_order_seq_cst)
            requires (!Const)
        {
            lib::bug_on(_data == nullptr);

            const auto idx = index / word_bits;
            const auto mask = get_bit(index);

            if constexpr (Atomic)
            {
                if (!std::is_constant_evaluated())
                {
                    std::atomic_ref ref { _data[idx] };
                    const auto old = value
                        ? ref.fetch_or(mask, order)
                        : ref.fetch_and(static_cast<Word>(~mask), order);
                    return old & mask;
                }
            }

            const auto old = _data[idx];
            _data[idx] = value ? old | mask : old & ~mask;
            return old & mask;
        }

        bool flip(std::size_t index, std::memory_order order = std::memory_order_seq_cst)
            requires (Atomic)
        {
            lib::bug_on(_data == nullptr);

            const auto mask = get_bit(index);
            std::atomic_ref ref { _data[index / word_bits] };
            return ref.fetch_xor(mask, order) & mask;
        }

        std::optional<std::size_t> allocate(
            std::size_t start = 0, std::memory_order order = std::memory_order_seq_cst
        ) requires (Atomic)
        {
            if (start >= _count)
                return std::nullopt;

            lib::bug_on(_data == nullptr);
            const auto first = start / word_bits;

            for (std::size_t idx = first; idx < size_words(); idx++)
            {
                const Word before = idx == first
                    ? (static_cast<Word>(1) << (start % word_bits)) - 1 : 0;
                const Word usable = valid_bits(idx) & ~before;

                std::atomic_ref ref { _data[idx] };
                auto cur = ref.load(std::memory_order_relaxed);

                while (const Word free = ~cur & usable)
                {
                    const auto pos = std::countr_zero(free);
                    const Word mask = static_cast<Word>(1) << pos;

                    const auto old = ref.fetch_or(mask, order);
                    if ((old & mask) == 0)
                        return idx * word_bits + pos;

                    cur = old | mask;
                }
            }
            return std::nullopt;
        }

        constexpr std::optional<std::size_t> find(
            bool value, std::size_t start = 0,
            std::memory_order order = std::memory_order_relaxed
        ) const
        {
            if (start >= _count)
                return std::nullopt;

            lib::bug_on(_data == nullptr);
            const auto first = start / word_bits;

            for (std::size_t idx = first; idx < size_words(); idx++)
            {
                const Word before = idx == first
                    ? (static_cast<Word>(1) << (start % word_bits)) - 1 : 0;
                const Word usable = valid_bits(idx) & ~before;

                const Word word = load(idx, order);
                const Word found = (value ? word : static_cast<Word>(~word)) & usable;
                if (found)
                    return idx * word_bits + std::countr_zero(found);
            }
            return std::nullopt;
        }

        constexpr bit operator[](std::size_t index) requires (!Const);
        constexpr bool operator[](std::size_t index) const { return get(index); }

        constexpr std::size_t length() const { return _count; }
        constexpr std::size_t size() const { return _count; }

        constexpr std::size_t size_words() const { return div_roundup(_count, word_bits); }
        constexpr std::size_t size_bytes() const { return size_words() * sizeof(Word); }

        constexpr bool empty(std::memory_order order = std::memory_order_relaxed) const
        {
            const auto words = size_words();
            if (words == 0)
                return true;

            lib::bug_on(_data == nullptr);

            for (std::size_t i = 0; i < words; i++)
            {
                if (load(i, order) & valid_bits(i))
                    return false;
            }
            return true;
        }

        constexpr std::size_t count(std::memory_order order = std::memory_order_relaxed) const
        {
            const auto words = size_words();
            if (words == 0)
                return 0;

            lib::bug_on(_data == nullptr);

            std::size_t total = 0;
            for (std::size_t i = 0; i < words; i++)
                total += std::popcount(static_cast<Word>(load(i, order) & valid_bits(i)));

            return total;
        }

        constexpr word_t *data() const
        {
            lib::bug_on(_data == nullptr);
            return _data;
        }
    };

    template<bool Atomic, bool Const, typename Word>
        requires (std::unsigned_integral<Word> && !(Atomic && Const))
    struct bitmap_view_base<Atomic, Const, Word>::bit
    {
        bitmap_view_base<Atomic, Const, Word> view;
        std::size_t index;

        constexpr bit &operator=(bool value)
        {
            view.set(index, value);
            return *this;
        }

        constexpr operator bool() const { return view.get(index); }
    };

    template<bool Atomic, bool Const, typename Word>
        requires (std::unsigned_integral<Word> && !(Atomic && Const))
    constexpr auto bitmap_view_base<Atomic, Const, Word>::operator[](std::size_t index)
        -> bit requires (!Const)
    {
        lib::bug_on(_data == nullptr);
        return { *this, index };
    }

    template<typename Word = std::uint8_t>
    using word_bitmap_view = bitmap_view_base<false, false, Word>;
    template<typename Word = std::uint8_t>
    using const_word_bitmap_view = bitmap_view_base<false, true, Word>;
    template<typename Word = std::uint8_t>
    using atomic_word_bitmap_view = bitmap_view_base<true, false, Word>;

    using bitmap_view = word_bitmap_view<>;
    using const_bitmap_view = const_word_bitmap_view<>;
    using atomic_bitmap_view = atomic_word_bitmap_view<>;

    template<typename Word = std::uint8_t>
        requires std::unsigned_integral<Word>
    class basic_bitmap
    {
        private:
        static constexpr std::size_t word_bits = sizeof(Word) * 8;

        Word *_data;
        std::size_t _count;
        bool _allocated;

        public:
        using view_t = word_bitmap_view<Word>;
        using const_view_t = const_word_bitmap_view<Word>;
        using atomic_view_t = atomic_word_bitmap_view<Word>;

        friend constexpr void swap(basic_bitmap &lhs, basic_bitmap &rhs)
        {
            using std::swap;
            swap(lhs._data, rhs._data);
            swap(lhs._count, rhs._count);
            swap(lhs._allocated, rhs._allocated);
        }

        constexpr basic_bitmap()
            : _data { nullptr }, _count { 0 }, _allocated { false } { }
        constexpr basic_bitmap(Word *data, std::size_t count)
            : _data { data }, _count { count }, _allocated { false } { }

        basic_bitmap(std::size_t count)
            : _data { new Word[div_roundup(count, word_bits)] { } },
              _count { count }, _allocated { true } { }

        basic_bitmap(const basic_bitmap &other) : basic_bitmap { other._count }
        {
            std::memcpy(_data, other._data, size_bytes());
        }

        basic_bitmap &operator=(const basic_bitmap &other)
        {
            if (this != &other)
            {
                basic_bitmap copy { other };
                swap(*this, copy);
            }
            return *this;
        }

        constexpr basic_bitmap(basic_bitmap &&other) : basic_bitmap { }
        {
            swap(*this, other);
        }

        constexpr basic_bitmap &operator=(basic_bitmap &&other)
        {
            if (this != &other)
                swap(*this, other);
            return *this;
        }

        constexpr ~basic_bitmap()
        {
            if (_allocated)
                delete[] _data;
        }

        template<typename Self>
        constexpr auto view(this Self &self)
        {
            if constexpr (std::is_const_v<Self>)
                return const_view_t { self._data, self._count };
            else
                return view_t { self._data, self._count };
        }

        constexpr operator view_t() { return view(); }
        constexpr operator const_view_t() const { return view(); }

        constexpr atomic_view_t atomic_view() { return { _data, _count }; }

        constexpr void initialise(Word *data, std::size_t count)
        {
            lib::bug_on(_data != nullptr);
            _data = data;
            _count = count;
        }

        void initialise(std::size_t count)
        {
            lib::bug_on(_data != nullptr);
            _data = new Word[div_roundup(count, word_bits)] { };
            _count = count;
            _allocated = true;
        }

        constexpr void clear(int ch = 0) { view().clear(ch); }
        constexpr bool get(std::size_t index) const { return view().get(index); }
        constexpr bool set(std::size_t index, bool value) { return view().set(index, value); }
        constexpr auto operator[](std::size_t index) { return view()[index]; }

        constexpr std::size_t length() const { return _count; }
        constexpr std::size_t size() const { return _count; }
        constexpr std::size_t size_words() const { return div_roundup(_count, word_bits); }
        constexpr std::size_t size_bytes() const { return size_words() * sizeof(Word); }
        constexpr bool empty() const { return view().empty(); }
        constexpr std::size_t count() const { return view().count(); }

        constexpr auto data(this auto &self) { return self.view().data(); }
    };

    using bitmap = basic_bitmap<>;

    template<std::size_t NBits, typename Word = std::uint8_t>
        requires (NBits != 0 && std::unsigned_integral<Word>)
    class static_bitmap
    {
        private:
        static constexpr std::size_t word_bits = sizeof(Word) * 8;
        Word _storage[div_roundup(NBits, word_bits)] { };

        public:
        using view_t = word_bitmap_view<Word>;
        using const_view_t = const_word_bitmap_view<Word>;
        using atomic_view_t = atomic_word_bitmap_view<Word>;

        template<typename Self>
        constexpr auto view(this Self &self)
        {
            if constexpr (std::is_const_v<Self>)
                return const_view_t { self._storage, NBits };
            else
                return view_t { self._storage, NBits };
        }

        constexpr operator view_t() { return view(); }
        constexpr operator const_view_t() const { return view(); }

        constexpr atomic_view_t atomic_view() { return { _storage, NBits }; }

        constexpr void clear(int ch = 0) { view().clear(ch); }
        constexpr bool get(std::size_t index) const { return view().get(index); }
        constexpr bool set(std::size_t index, bool value) { return view().set(index, value); }
        constexpr auto operator[](std::size_t index) { return view()[index]; }

        constexpr std::size_t length() const { return NBits; }
        constexpr std::size_t size() const { return NBits; }
        constexpr std::size_t size_words() const { return std::size(_storage); }
        constexpr std::size_t size_bytes() const { return sizeof(_storage); }
        constexpr bool empty() const { return view().empty(); }
        constexpr std::size_t count() const { return view().count(); }

        constexpr auto data(this auto &self) { return self._storage; }
    };
} // export namespace lib
