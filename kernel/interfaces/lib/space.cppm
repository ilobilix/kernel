// Copyright (C) 2024  ilobilo

export module lib:space;

import :mmio;
import :io;
import std;

export namespace lib
{
    template<typename Type>
    struct bitfield
    {
        using type = Type;

        std::size_t bits = 0;
        std::size_t shift = 0;
    };

    template<typename Type>
    struct reg
    {
        using type = Type;
        type value = 0;
    };

    class space
    {
        private:
        std::uintptr_t base;

        template<typename Type>
        class bits
        {
            using type = Type;
            type value;

            constexpr operator type() const { return value; }

            constexpr bits &operator|=(type mask)
            {
                value |= mask;
                return *this;
            }

            constexpr bits &operator&=(type mask)
            {
                value &= mask;
                return *this;
            }
        };

        template<typename Type>
        auto read(std::uintptr_t offset) const
        {
            auto addr = base + offset;
            if constexpr (io::supported)
            {
                if (addr < 0xFFFF)
                    return io::in<Type>(addr);
            }
            return mmio::in<Type>(addr);
        }

        template<typename Type>
        void write(std::uintptr_t offset, Type val) const
        {
            auto addr = base + offset;
            if constexpr (io::supported)
            {
                if (addr < 0xFFFF)
                {
                    io::out<Type>(addr, val);
                    return;
                }
            }
            mmio::out<Type>(addr, val);
        }

        public:
        constexpr space(std::uintptr_t base) : base { base } { }

        template<typename Type>
        bits<Type> load(reg<Type> reg)
        {
            return read<Type>(reg.value);
        }

        template<typename Type>
        bits<Type> load(std::uintptr_t offset)
        {
            return read<Type>(offset);
        }

        template<typename Type>
        void store(reg<Type> reg, bits<Type> val)
        {
            return write<Type>(reg, val.value);
        }

        template<typename Type>
        void store(std::uintptr_t offset, bits<Type> val)
        {
            return write<Type>(offset, val.value);
        }

        template<typename Type>
        void store(reg<Type> reg, Type val)
        {
            return write<Type>(reg, val);
        }

        template<typename Type>
        void store(std::uintptr_t offset, Type val)
        {
            return write<Type>(offset, val);
        }
    };
} // export namespace lib