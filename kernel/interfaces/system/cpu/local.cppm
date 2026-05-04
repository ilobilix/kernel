// Copyright (C) 2024-2026  ilobilo

export module system.cpu.local;

import system.cpu.arch;
import std;

extern "C" char __start_percpu[];
extern "C" char __end_percpu[];

namespace cpu::local
{
    template<typename Type, auto Member>
    concept helper_enabled = (std::is_class_v<Type> || std::is_union_v<Type>) &&
        std::is_member_object_pointer_v<decltype(Member)>;

    void begin_access();
    void end_access();
} // namespace cpu::local

export namespace cpu
{
    struct processor
    {
        // do not move
        processor *self;
        std::uintptr_t stack_top;

        std::size_t idx;
        std::size_t arch_id;

        std::uint64_t asid_gen = 1;
        std::size_t next_asid = 1;

        std::atomic_bool in_interrupt = false;

        std::atomic_bool online = false;
    };

    namespace local
    {
        template<typename Type>
        class storage
        {
            private:
            alignas(Type) std::byte _storage[sizeof(Type)];

            public:
            constexpr storage() = default;

            template<typename NType>
            void write(const NType &value, std::uintptr_t off = 0)
            {
                begin_access();
                const auto base = self_addr();
                const auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(_storage));
                const auto peraddr = reinterpret_cast<std::uintptr_t>(__start_percpu);
                const auto offset = addr - peraddr + off;

                *const_cast<NType *>(std::launder(reinterpret_cast<volatile NType *>(reinterpret_cast<std::uintptr_t>(base) + offset))) = value;
                end_access();
            }

            template<typename NType, auto Member>
                requires helper_enabled<Type, Member>
            void write(const NType &value)
            {
                begin_access();
                unsafe_get().*Member = value;
                end_access();
            }

            template<typename NType, auto Member>
                requires helper_enabled<Type, Member>
            void write(NType &&value)
            {
                begin_access();
                unsafe_get().*Member = std::forward<NType>(value);
                end_access();
            }

            template<typename NType = Type>
            NType read(std::uintptr_t off = 0) const
            {
                begin_access();
                const auto base = self_addr();
                const auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(_storage));
                const auto peraddr = reinterpret_cast<std::uintptr_t>(__start_percpu);
                const auto offset = addr - peraddr + off;

                auto result = *const_cast<NType *>(std::launder(reinterpret_cast<volatile NType *>(reinterpret_cast<std::uintptr_t>(base) + offset)));
                end_access();
                return result;
            }

            template<typename NType, auto Member>
                requires helper_enabled<Type, Member>
            NType read() const
            {
                begin_access();
                auto ret = unsafe_get().*Member;
                end_access();
                return ret;
            }

            Type &unsafe_get(std::uintptr_t base = self_addr()) const
            {
                const auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(_storage));
                const auto peraddr = reinterpret_cast<std::uintptr_t>(__start_percpu);
                const auto offset = addr - peraddr;
                return *const_cast<Type *>(std::launder(reinterpret_cast<volatile Type *>(reinterpret_cast<std::uintptr_t>(base) + offset)));
            }

            template<typename ...Args>
            void initialise(std::uintptr_t base, Args &&...args) const
            {
                const auto addr = reinterpret_cast<std::uintptr_t>(std::addressof(_storage));
                const auto peraddr = reinterpret_cast<std::uintptr_t>(__start_percpu);
                const auto offset = addr - peraddr;

                auto ptr = reinterpret_cast<void *>(base + offset);
                new(ptr) Type { std::forward<Args>(args)... };
            }
        };

        processor *request(std::size_t aid);

        processor *nth(std::size_t n);
        std::uintptr_t nth_base(std::size_t n);

        std::size_t arch2idx(std::size_t arch_id);

        bool available();
    } // namespace local

    local::storage<processor> &self();
} // export namespace cpu
