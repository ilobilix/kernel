// Copyright (C) 2024-2026  ilobilo

export module lib:syscall;

import :errno;
import :log;
import :types;
import :string;
import system.cpu.regs;
import magic_enum;
import std;

namespace lib::syscall
{
    export bool log_enabled = false;

    template<typename Type, typename CType = remove_address_space_t<Type>>
    using to_formattable_ptr = typename std::conditional_t<
        std::is_pointer_v<CType>,
        std::conditional_t<
            std::is_constructible_v<std::string_view, CType>, user_string, const void *>,
        std::conditional_t<std::is_unsigned_v<CType>, const void *, Type>>;

    template<typename... Ts>
    auto ptr(const std::tuple<Ts...> &tup)
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"
        return std::apply(
            [](const auto &...args) {
                return std::tuple { ((__force to_formattable_ptr<Ts>)args)... };
            },
            tup
        );
#pragma clang diagnostic pop
    }

    export template<typename Type, std::size_t N>
    concept getter = requires(cpu::registers *regs) {
        { Type::get_args(regs) } -> std::same_as<std::array<std::uintptr_t, N>>;
    };

    std::pair<std::size_t, std::size_t> get_ptid();

    export template<std::size_t N, getter<N> Getter>
    class entry
    {
        private:
        std::string_view name;
        void *handler;
        bool log_exit;
        std::uintptr_t (*invoker)(cpu::registers *, std::string_view, void *, bool);

        public:
        constexpr entry(std::string_view name, const auto &func, bool log_exit = false)
            : name { name }, handler { reinterpret_cast<void *>(func) }, log_exit { log_exit },
              invoker {
                  [](cpu::registers *regs, std::string_view name, void *handler,
                     bool log_exit) -> std::uintptr_t {
                      using sign = typename lib::signature<std::remove_cvref_t<decltype(func)>>;
                      constexpr bool is_void = std::same_as<typename sign::return_type, void>;
                      static_assert(
                          std::is_trivially_default_constructible_v<typename sign::return_type> ||
                          is_void
                      );

                      const auto arr = Getter::get_args(regs);
                      typename sign::args_type args;

                      [&]<std::size_t... I>(std::index_sequence<I...>) {
                          (
                              [&]<typename Type>(Type &item) {
                                  static_assert(std::is_trivially_default_constructible_v<Type>);
                                  item = Type(arr[I]);
                              } (std::get<I>(args)),
                              ...);
                      } (std::make_index_sequence<std::tuple_size_v<typename sign::args_type>> { });

                      std::size_t pid = 0, tid = 0;
                      if (log_enabled)
                      {
                          std::tie(pid, tid) = get_ptid();
                          if constexpr (std::same_as<typename sign::args_type, std::tuple<>>)
                              lib::debug("syscall: [{}:{}]: {}()", pid, tid, name);
                          else
                              lib::debug("syscall: [{}:{}]: {}{}", pid, tid, name, ptr(args));
                      }

                      std::uintptr_t uptr_ret = 0;
                      if constexpr (!is_void)
                      {
                          const auto ret =
                              std::apply(reinterpret_cast<sign::type *>(handler), args);
                          uptr_ret = std::uintptr_t(ret);
                      }
                      else
                          std::apply(reinterpret_cast<sign::type *>(handler), args);

                      if (const auto iptr_ret = static_cast<std::intptr_t>(uptr_ret); iptr_ret < 0)
                      {
                          if (log_enabled)
                          {
                              lib::debug(
                                  "syscall: [{}:{}]: {} -> {}", pid, tid, name,
                                  magic_enum::enum_name(static_cast<errnos>(-iptr_ret))
                              );
                          }
                          return uptr_ret;
                      }

                      if (log_enabled && log_exit)
                      {
                          if constexpr (is_void)
                              lib::debug("syscall: [{}:{}]: {} -> void", pid, tid, name);
                          else if constexpr (std::is_pointer_v<typename sign::return_type>)
                              lib::debug(
                                  "syscall: [{}:{}]: {} -> {}", pid, tid, name,
                                  reinterpret_cast<const void *>(uptr_ret)
                              );
                          else
                              lib::debug("syscall: [{}:{}]: {} -> {}", pid, tid, name, uptr_ret);
                      }

                      if constexpr (is_void)
                          return 0;
                      return uptr_ret;
                  }
              }
        { }

        constexpr entry() : name { "unknown" }, handler { nullptr }, log_exit { false }, invoker { } { };

        constexpr entry(const entry &other) = default;
        constexpr entry &operator=(const entry &other) = default;

        bool is_valid() const { return handler != nullptr && invoker != nullptr; }

        std::uintptr_t invoke(cpu::registers *regs)
        {
            return invoker(regs, name, handler, log_exit);
        }
    };
} // namespace lib::syscall
