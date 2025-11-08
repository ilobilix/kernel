// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>
#include <user.h>

export module lib:syscall;

import :log;
import :types;
import :unused;
import system.cpu;
import magic_enum;
import cppstd;

namespace lib::syscall
{
    template<typename Type>
    using to_formattable_ptr =
        typename std::conditional_t<
            std::is_pointer_v<Type>,
            std::conditional_t<
                std::is_constructible_v<
                    std::string_view,
                    Type
                >,
                nullable_string,
                const void *
            >, Type
        >;

    template<typename ...Ts>
    auto ptr(const std::tuple<Ts...> &tup)
    {
        return std::apply(
            [](const auto &...args) {
                return std::tuple<to_formattable_ptr<Ts>...>(
                    ((__force remove_address_space_t<std::remove_cvref_t<decltype(args)>>)args)...
                );
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
        std::uintptr_t (*invoker)(cpu::registers *, std::string_view, void *);

        public:
        constexpr entry(std::string_view name, const auto &func) : name { name }, handler { reinterpret_cast<void *>(func) },
            invoker {
                [](cpu::registers *regs, std::string_view name, void *handler) -> std::uintptr_t
                {
                    using sign = typename lib::signature<std::remove_cvref_t<decltype(func)>>;
                    static_assert(std::is_trivially_default_constructible_v<typename sign::return_type>);

                    const auto arr = Getter::get_args(regs);
                    typename sign::args_type args;

                    std::size_t i = 0;
                    std::apply([&](auto &&...args) {
                        (std::invoke([&]<typename Type>(Type &arg) {
                            static_assert(std::is_trivially_default_constructible_v<Type>);
                            arg = Type(arr[i++]);
                        }, args), ...);
                    }, args);

#if ILOBILIX_SYSCALL_LOG
                    const auto [pid, tid] = get_ptid();
                    if constexpr (std::same_as<typename sign::args_type, std::tuple<>>)
                        log::debug("syscall: [{}:{}]: {}()", pid, tid, name);
                    else
                        log::debug("syscall: [{}:{}]: {}{}", pid, tid, name, ptr(args));
#else
                    lib::unused(name);
#endif

                    errno = no_error;
                    const auto ret = std::apply(reinterpret_cast<sign::type *>(handler), args);
                    const auto uptr_ret = std::uintptr_t(ret);
                    const auto iptr_ret = std::intptr_t(ret);

                    const auto error = magic_enum::enum_cast<errnos>(errno);
                    if (error.has_value() && iptr_ret < 0)
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
                    if constexpr (std::is_same_v<typename sign::return_type, void>)
                        log::debug("syscall: [{}:{}]: {} -> void", pid, tid, name);
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
            return invoker(regs, name, handler);
        }
    };
} // namespace lib::syscall