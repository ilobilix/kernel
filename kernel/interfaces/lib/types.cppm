// Copyright (C) 2024-2025  ilobilo

export module lib:types;

import :log;
import fmt;
import cppstd;

export namespace lib
{
    template<std::size_t N>
    struct bits2uint;

    template<>
    struct bits2uint<8> { using type = std::uint8_t; };

    template<>
    struct bits2uint<16> { using type = std::uint16_t; };

    template<>
    struct bits2uint<32> { using type = std::uint32_t; };

    template<>
    struct bits2uint<64> { using type = std::uint64_t; };

    template<std::size_t N>
    using bits2uint_t = bits2uint<N>::type;

    template<typename ...Funcs>
    struct overloaded : Funcs... { using Funcs::operator()...; };

    template<typename ...Funcs>
    overloaded(Funcs ...) -> overloaded<Funcs...>;

    template<typename>
    class signature;

    template<typename Ret, typename ...Args>
    class signature<Ret(Args...)>
    {
        public:
        using type = Ret(Args...);
        using return_type = Ret;
        using args_type = std::tuple<Args...>;
    };

    template<typename Type>
    struct remove_address_space { using type = Type; };

    template<typename Type, std::size_t N>
    struct remove_address_space<Type __attribute__((address_space(N)))> { using type = Type; };

    template<typename Type>
    struct remove_address_space_helper
    {
        using base_type = typename std::remove_pointer<Type>::type;
        using clean_type = typename remove_address_space<base_type>::type;
        using type = clean_type *;
    };

    template<typename Type>
    struct remove_address_space<Type *> : remove_address_space_helper<Type *> { };

    template<typename Type>
    struct remove_address_space<const Type> { using type = const typename remove_address_space<Type>::type; };

    template<typename Type>
    struct remove_address_space<const Type *> { using type = const typename remove_address_space<Type>::type *; };

    template<typename Type>
    using remove_address_space_t = typename remove_address_space<Type>::type;

    template<typename Type>
    struct has_address_space
    {
        static constexpr bool value = !std::is_same_v<Type, remove_address_space_t<Type>>;
    };

    template<typename Type>
    inline constexpr bool has_address_space_v = has_address_space<Type>::value;
} // export namespace lib

export
{
    using time_t = std::int64_t;
    using suseconds_t = std::int64_t;

    using clockid_t = std::int32_t;
    using pid_t = std::int32_t;
    using rlim_t = std::uint64_t;

    using dev_t = std::uint64_t;
    using ino_t = std::uint64_t;
    using mode_t = std::uint32_t;
    using nlink_t = std::uint64_t;
    using uid_t = std::uint32_t;
    using gid_t = std::uint32_t;
    using dev_t = std::uint64_t;
    using off_t = std::int64_t;
    using blksize_t = std::int64_t;
    using blkcnt_t = std::int64_t;
} // export