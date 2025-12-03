// Copyright (C) 2024-2025  ilobilo

export module lib:syscall;

import :errno;
import :log;
import :types;
import :string;
import :unused;
import system.cpu;
import magic_enum;
import std;

namespace lib::syscall
{
    template<typename Type, typename CType = remove_address_space_t<Type>>
    using to_formattable_ptr =
        typename std::conditional_t<
            std::is_pointer_v<CType>,
            std::conditional_t<
                std::is_constructible_v<std::string_view, CType>,
                user_string,
                const void *
            >, Type
        >;

    template<typename ...Ts>
    auto ptr(const std::tuple<Ts...> &tup)
    {
        return std::apply(
            [](const auto &...args) {
                return std::tuple {
                    ((__force to_formattable_ptr<Ts>)args)...
                };
            }, tup
        );
    }

    export template<typename Type, std::size_t N>
    concept getter = requires(cpu::registers *regs)
    {
        { Type::get_args(regs) } -> std::same_as<std::array<std::uintptr_t, N>>;
    };

    std::pair<std::size_t, std::size_t> get_ptid();

    export template<std::size_t N, getter<N> Getter>
    class entry
    {
        private:
        std::string_view name;
        void *handler;
        bool (*checker)(std::uintptr_t);
        std::uintptr_t (*invoker)(cpu::registers *, std::string_view, void *, bool (*)(std::uintptr_t));

        static bool default_checker(std::uintptr_t val)
        {
            return static_cast<std::intptr_t>(val) < 0;
        }

        public:
        constexpr entry(std::string_view name, const auto &func, decltype(checker) check = default_checker)
            : name { name }, handler { reinterpret_cast<void *>(func) }, checker { check },
            invoker {
                [](cpu::registers *regs, std::string_view name, void *handler, bool (*check)(std::uintptr_t)) -> std::uintptr_t
                {
                    using sign = typename lib::signature<std::remove_cvref_t<decltype(func)>>;
                    constexpr bool is_void = std::same_as<typename sign::return_type, void>;
                    static_assert(std::is_trivially_default_constructible_v<typename sign::return_type> || is_void);

                    const auto arr = Getter::get_args(regs);
                    typename sign::args_type args;

                    [&]<std::size_t ...I>(std::index_sequence<I...>)
                    {
                        ([&]<typename Type>(Type &item)
                        {
                            static_assert(std::is_trivially_default_constructible_v<Type>);
                            item = Type(arr[I]);
                        } (std::get<I>(args)), ...);
                    } (std::make_index_sequence<std::tuple_size_v<typename sign::args_type>> { });

#if ILOBILIX_SYSCALL_LOG
                    const auto [pid, tid] = get_ptid();
                    if constexpr (std::same_as<typename sign::args_type, std::tuple<>>)
                        log::debug("syscall: [{}:{}]: {}()", pid, tid, name);
                    else
                        log::debug("syscall: [{}:{}]: {}{}", pid, tid, name, ptr(args));
#else
                    lib::unused(name);
#endif

                    const auto _check = [&](auto value) { return is_void || check(value); };
                    std::uintptr_t uptr_ret = 0;

                    errno = no_error;
                    if constexpr (!is_void)
                    {
                        const auto ret = std::apply(reinterpret_cast<sign::type *>(handler), args);
                        uptr_ret = std::uintptr_t(ret);
                    }
                    else std::apply(reinterpret_cast<sign::type *>(handler), args);

                    const auto error = magic_enum::enum_cast<errnos>(errno);
                    if (error.has_value() && _check(uptr_ret))
                    {
#if ILOBILIX_SYSCALL_LOG
                        log::debug(
                            "syscall: [{}:{}]: {} -> {}", pid, tid, name,
                            magic_enum::enum_name(error.value())
                        );
#endif
                        return -errno;
                    }
#if ILOBILIX_SYSCALL_LOG
                    if constexpr (is_void)
                    {
                        log::debug("syscall: [{}:{}]: {} -> void", pid, tid, name);
                        return 0;
                    }
                    else if constexpr (std::is_pointer_v<typename sign::return_type>)
                        log::debug("syscall: [{}:{}]: {} -> {}", pid, tid, name, reinterpret_cast<const void *>(uptr_ret));
                    else
                        log::debug("syscall: [{}:{}]: {} -> {}", pid, tid, name, uptr_ret);
#endif
                    return uptr_ret;
                }
            } { }

        constexpr entry() : name { "unknown" }, handler { nullptr }, invoker { } { };

        constexpr entry(const entry &other) = default;
        constexpr entry &operator=(const entry &other) = default;

        bool is_valid() const
        {
            return handler != nullptr && invoker != nullptr;
        }

        std::uintptr_t invoke(cpu::registers *regs)
        {
            return invoker(regs, name, handler, checker);
        }
    };
} // namespace lib::syscall