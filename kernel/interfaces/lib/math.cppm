// Copyright (C) 2024-2025  ilobilo

export module lib:math;
import std;

extern "C++" std::uintptr_t (*get_hhdm_offset)();

namespace lib::detail
{
    template<typename Type>
    using get_ret_type =
        std::conditional_t<
            std::integral<Type>,
            std::conditional_t<
                std::unsigned_integral<Type>,
                std::uintptr_t, std::intptr_t
            >, Type
        >;
} // namespace lib::detail
export using uint128_t = unsigned _BitInt(128);
export using int128_t = _BitInt(128);

export namespace lib
{
    inline constexpr std::size_t kib(std::size_t num) { return num * 1024; }
    inline constexpr std::size_t mib(std::size_t num) { return kib(num) * 1024; }
    inline constexpr std::size_t gib(std::size_t num) { return mib(num) * 1024; }

    inline constexpr bool has_bits(std::unsigned_integral auto val, auto ...bits)
    {
        return ([](auto val, std::size_t bit) {
            return (val & (1ul << bit)) == (1ul << bit);
        } (val, bits) && ...);
    }

    inline bool ishh(auto val)
    {
        return std::uintptr_t(val) >= get_hhdm_offset();
    }

    template<typename Type, typename Ret = detail::get_ret_type<Type>>
    inline Ret tohh(Type val)
    {
        return ishh(val) ? Ret(val) : Ret(std::uintptr_t(val) + get_hhdm_offset());
    }

    template<typename Type, typename Ret = detail::get_ret_type<Type>>
    inline Ret fromhh(Type val)
    {
        return !ishh(val) ? Ret(val) : Ret(std::uintptr_t(val) - get_hhdm_offset());
    }

    inline constexpr auto align_down(std::unsigned_integral auto n, std::size_t a)
    {
        constexpr auto align_down_internal = [&](auto n, auto a) { return (n & ~(a - 1)); };
        return align_down_internal(std::uint64_t(n), std::uint64_t(a));
    }

    inline constexpr auto align_up(std::unsigned_integral auto n, std::size_t a)
    {
        return align_down(n + a - 1, a);
    }

    inline constexpr auto div_roundup(std::unsigned_integral auto n, std::size_t a)
    {
        return align_down(n + a - 1, a) / a;
    }

    inline constexpr auto div_rounddown(std::unsigned_integral auto n, std::size_t a)
    {
        return align_down(n, a) / a;
    }

    inline constexpr auto unique_from(std::unsigned_integral auto a) { return a; }

    template<typename ...Args>
    inline constexpr auto unique_from(auto a, Args &&...args)
    {
        static constexpr auto szudzik = [](std::size_t x, std::size_t y)
        {
            return (x >= y) ? ((x * x) + x + y) : ((y * y) + x);
        };

        if constexpr (sizeof...(Args) == 1)
            return szudzik(a, std::forward<Args>(args)...);

        return szudzik(a, unique_from(std::forward<Args>(args)...));
    }

    inline constexpr bool range_overlaps(std::uintptr_t start1, std::uintptr_t end1, std::uintptr_t start2, std::uintptr_t end2)
    {
        return start1 < end2 && start2 < end1;
    }

    inline constexpr std::pair<std::uintptr_t, std::uintptr_t> range_intersection(std::uintptr_t start1, std::uintptr_t end1, std::uintptr_t start2, std::uintptr_t end2)
    {
        if (!range_overlaps(start1, end1, start2, end2))
            return { 0, 0 };
        return { std::max(start1, start2), std::min(end1, end2) };
    }

    template<std::unsigned_integral Type>
    inline constexpr Type log2(Type val)
    {
        return std::bit_width<Type>(val) - 1;
    }

    inline constexpr auto pow2(std::size_t val)
    {
        return 1ull << val;
    }

    inline constexpr bool is_pow2(std::unsigned_integral auto num)
    {
        return !(num & (num - 1));
    }

    inline constexpr std::size_t next_pow2(std::size_t val)
    {
        val--;
        val |= val >> 1;
        val |= val >> 2;
        val |= val >> 4;
        val |= val >> 8;
        val |= val >> 16;
        val |= val >> 32;
        return ++val;
    }

    inline constexpr std::size_t pre_pow2(std::size_t val)
    {
        const auto np2 = next_pow2(val);
        return np2 == val ? np2 : np2 >> 1;
    }

    class freqfrac
    {
        private:
        int p;
        std::uint64_t n, freq;

        constexpr freqfrac(int p, std::uint64_t n, std::uint64_t freq)
            : p { p }, n { n }, freq { freq } { }

        public:
        constexpr freqfrac() : p { 0 }, n { 0 } { }
        constexpr freqfrac(std::uint64_t freq) { *this = init(freq); }

        constexpr freqfrac(const freqfrac &) = default;
        constexpr freqfrac(freqfrac &&) = default;

        static constexpr freqfrac init(std::uint64_t freq)
        {
            int p = lib::log2<std::uint64_t>(freq);
            std::uint64_t n = (1'000'000'000ull << p) / freq;
            return { p, n, freq };
        }

        constexpr freqfrac &operator=(const freqfrac &) = default;
        constexpr freqfrac &operator=(freqfrac &&) = default;

        constexpr freqfrac &operator=(std::uint64_t freq)
        {
            return *this = init(freq);
        }

        constexpr std::uint64_t nanos(uint128_t ticks) const
        {
            const auto res = (ticks * n) >> p;
            if (res >> 64)
                return std::numeric_limits<std::uint64_t>::max();
            return res;
        }

        constexpr std::uint64_t ticks(std::uint64_t nanos) const
        {
            return (nanos << p) / n;
        }

        constexpr std::uint64_t frequency() const { return freq; }
    };

    constexpr auto timestamp(std::uint16_t years, std::uint8_t months, std::uint8_t days, std::uint8_t hours, std::uint8_t minutes, std::uint8_t seconds)
    {
        constexpr auto days_from_civil = [](std::int64_t years, std::uint64_t months, std::uint64_t days)
        {
            years -= (months <= 2);
            const auto era = (years >= 0 ? years : years - 399) / 400;
            const auto yoe = static_cast<std::uint64_t>(years - era * 400);
            const auto doy = (153 * (months > 2 ? months - 3 : months + 9) + 2) / 5 + days - 1;
            const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
        };
        std::uint64_t value = days_from_civil(years, months, days);

        return (value * 86400) + (hours * 3600) + (minutes * 60) + seconds;
    }
    inline constexpr auto epoch = timestamp(1970, 1, 1, 0, 0, 0);

    constexpr std::tuple<std::uint16_t, std::uint8_t, std::uint8_t> date_from(std::uint64_t unix)
    {
        unix /= 86400;
        unix += 719468;

        const auto era = (unix >= 0 ? unix : unix - 146096) / 146097;
        const auto doe = static_cast<std::uint64_t>(unix - era * 146097);
        const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const auto year = static_cast<std::uint16_t>(yoe + era * 400);
        const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const auto mp = (5 * doy + 2) / 153;
        const auto day = static_cast<std::uint8_t>(doy - (153 * mp + 2) / 5 + 1);
        const auto month = static_cast<std::uint8_t>(mp < 10 ? mp + 3 : mp - 9);
        return { year + (month <= 2), month, day };
    }

    constexpr std::tuple<std::uint8_t, std::uint8_t, std::uint8_t> time_from(std::uint64_t unix)
    {
        unix %= 86400;
        return { unix / 3600, unix / 60 % 60, unix % 60 };
    }
} // export namespace lib