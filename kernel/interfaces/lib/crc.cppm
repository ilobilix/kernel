// Copyright (C) 2024-2026  ilobilo

export module lib:crc;

import std;

export namespace lib
{
    class crc32
    {
        static constexpr std::uint32_t poly = 0xEDB88320;

        private:
        static constexpr std::array<std::uint32_t, 256> table = [] {
            std::array<std::uint32_t, 256> table { };
            std::uint32_t crc = 1;
            for (std::uint32_t i = 128; i; i >>= 1)
            {
                crc = (crc >> 1) ^ (crc & 1 ? poly : 0u);
                for (std::uint32_t j = 0; j < 256; j += 2 * i)
                    table[i + j] = crc ^ table[j];
            }
            return table;
        } ();

        public:
        template<typename Type>
        static std::uint32_t compute(std::span<Type> data)
        {
            const auto *bytes = reinterpret_cast<const std::uint8_t *>(data.data());
            std::uint32_t crc = 0xFFFFFFFF;
            for (std::size_t i = 0; i < data.size_bytes(); i++)
                crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFF;
        }
    };
} // export namespace lib
