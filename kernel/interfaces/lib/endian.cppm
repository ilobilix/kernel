// Copyright (C) 2024-2025  ilobilo

export module lib:endian;
import std;

export namespace lib
{
    template<std::endian New, std::endian Old = std::endian::native>
    inline constexpr auto convert_endian(std::integral auto num)
    {
        if constexpr (New == Old)
            return num;

        return std::byteswap(num);
    }

    template<std::endian New>
    inline constexpr auto to_endian(std::integral auto num)
    {
        return convert_endian<New, std::endian::native>(num);
    }

    template<std::endian Old>
    inline constexpr auto from_endian(std::integral auto num)
    {
        return convert_endian<std::endian::native, Old>(num);
    }

    template<std::integral Type, std::endian E>
    class endian_storage
    {
        private:
        Type value;

        public:
        using type = Type;

        constexpr endian_storage() = default;
        constexpr endian_storage(const endian_storage &) = default;
        constexpr endian_storage(endian_storage &&) = default;

        constexpr endian_storage(Type value)
            : value { convert_endian<E, std::endian::native>(static_cast<Type>(value)) } { }

        constexpr endian_storage &operator=(const endian_storage &) = default;
        constexpr endian_storage &operator=(endian_storage &&) = default;

        constexpr endian_storage &operator=(Type value)
        {
            store(value);
            return *this;
        }

        constexpr Type data()
        {
            return value;
        }

        constexpr Type load()
        {
            return convert_endian<std::endian::native, E>(value);
        }

        constexpr void store(Type value)
        {
            value = convert_endian<E, std::endian::native>(value);
        }

        constexpr operator Type()
        {
            return load();
        }

        constexpr bool operator==(endian_storage rhs)
        {
            return value == rhs.value;
        }

        constexpr bool operator==(Type rhs)
        {
            return load() == rhs;
        }

        constexpr auto operator<=>(endian_storage rhs)
        {
            return value <=> rhs.value;
        }

        constexpr auto operator<=>(Type rhs)
        {
            return load() <=> rhs;
        }
    };

    using big_u16_t = endian_storage<std::uint16_t, std::endian::big>;
    using big_u32_t = endian_storage<std::uint32_t, std::endian::big>;
    using big_u64_t = endian_storage<std::uint64_t, std::endian::big>;
} // export namespace lib