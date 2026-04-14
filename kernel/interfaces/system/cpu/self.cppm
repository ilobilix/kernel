// Copyright (C) 2024-2025  ilobilo

module;

#include <cerrno>

export module system.cpu.self;

import system.cpu.arch;
import std;

extern "C" char __start_percpu[];
extern "C" char __end_percpu[];

export namespace cpu
{
    extern "C++"
    {
        struct processor
        {
            // do not move
            processor *self;
            std::uintptr_t stack_top;

            std::size_t idx;
            std::size_t arch_id;

            std::atomic_bool in_interrupt;

            errnos err = no_error;
            std::atomic_bool online = false;
        };

        processor *self();
    } // extern "C++"

    namespace local
    {
        extern "C++" bool available();

        template<typename Type, std::size_t Size = sizeof(Type)>
        class storage
        {
            private:
            alignas(Type) std::byte _storage[Size];

            public:
            Type &get(std::uintptr_t base = self_addr()) const
            {
                const auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(_storage));
                const auto peraddr = reinterpret_cast<std::uintptr_t>(__start_percpu);
                const auto offset = addr - peraddr;
                return *const_cast<Type *>(std::launder(reinterpret_cast<volatile Type *>(reinterpret_cast<std::uintptr_t>(base) + offset)));
            }

            Type *operator->() { return std::addressof(get()); }
            Type &operator=(const Type &rhs) { return get() = rhs; }
            auto &operator[](std::size_t idx) { return get()[idx]; }

            template<typename ...Args>
            void initialise_base(std::uintptr_t base, Args &&...args) const
            {
                const auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(_storage));
                const auto peraddr = reinterpret_cast<std::uintptr_t>(__start_percpu);
                const auto offset = addr - peraddr;

                auto ptr = reinterpret_cast<void *>(base + offset);
                new(ptr) Type { std::forward<Args>(args)... };
            }

            template<typename ...Args>
            void initialise(Args &&...args)
            {
                initialise_base(self_addr(), args...);
            }
        };
    } // namespace local
} // export namespace cpu