// Copyright (C) 2024-2025  ilobilo

export module system.net:checksum;
import std;

export namespace net
{
    struct checksum
    {
        private:
        std::uint32_t _value = 0;

        public:
        constexpr void update(std::uint16_t n)
        {
            _value += n;
            while (_value >> 16)
                _value = (_value >> 16) + (_value & 0xFFFF);
        }

        constexpr void update(const std::byte *data, std::size_t size)
        {
            static_assert(sizeof(std::byte) == sizeof(std::uint8_t));
            auto bytes = data;

            if (size & 0x01)
            {
                size--;
                update(static_cast<std::uint16_t>(bytes[size]) << 8);
            }

            for (std::size_t i = 0; i < size; i += 2)
                update((static_cast<std::uint16_t>(bytes[i]) << 8) | static_cast<std::uint8_t>(bytes[i + 1]));
        }

        constexpr std::uint16_t operator()(const std::byte *data, std::size_t size)
        {
            update(data, size);
            return value();
        }

        constexpr std::uint16_t value() { return ~_value; }
        constexpr void reset() { _value = 0; }

        static constexpr std::uint16_t compute(const std::byte *data, std::size_t size)
        {
            checksum cs;
            return cs(data, size);
        }
    };
} // export namespace net