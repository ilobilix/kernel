// Copyright (C) 2024-2026  ilobilo

import system.acpi;
import system.chrono;
import drivers.timers;
import lib;
import std;

namespace x86_64::rtc
{
    namespace
    {
        enum class reg : std::uint8_t
        {
            seconds = 0x00,
            minutes = 0x02,
            hours = 0x04,
            weekday = 0x06, // sunday = 1
            day = 0x07,
            month = 0x08,
            year = 0x09,
            // century = 0x32,
            status_a = 0x0A,
            status_b = 0x0B
        };

        std::uint8_t century_reg = 0;

        inline constexpr std::uint8_t bcd2bin(std::uint8_t bcd)
        {
            return (bcd & 0x0F) + ((bcd >> 4) * 10);
        }

        std::uint8_t read(reg reg)
        {
            lib::io::out<8>(0x70, std::to_underlying(reg) & 0x7F);
            return lib::io::in<8>(0x71);
        }

        bool is_updating()
        {
            return (read(reg::status_a) & 0x80) != 0;
        }

        std::uint64_t get_datetime()
        {
            struct datetime
            {
                std::uint8_t second;
                std::uint8_t minute;
                std::uint8_t hour;
                std::uint8_t day;
                std::uint8_t month;
                std::uint8_t year;
                std::uint8_t century;

                constexpr datetime() = default;

                bool operator==(const datetime &rhs) const
                {
                    return
                        second == rhs.second && minute == rhs.minute &&
                        hour == rhs.hour && day == rhs.day &&
                        month == rhs.month && year == rhs.year &&
                        century == rhs.century;
                }

                void read_all()
                {
                    second = read(reg::seconds);
                    minute = read(reg::minutes);
                    hour = read(reg::hours);
                    day = read(reg::day);
                    month = read(reg::month);
                    year = read(reg::year);
                    if (century_reg)
                        century = read(static_cast<reg>(century_reg));
                }
            };

            datetime time;
            datetime last;

            while (is_updating())
                asm volatile ("pause");

            time.read_all();

            do {
                last = time;
                while (is_updating())
                    asm volatile ("pause");
                time.read_all();
            } while (time != last);

            const auto status_b = read(reg::status_b);
            const bool is_bcd = !(status_b & (1 << 2));

            if (is_bcd)
            {
                time.second = bcd2bin(time.second);
                time.minute = bcd2bin(time.minute);
                time.hour = bcd2bin(time.hour & 0x7F) | (time.hour & 0x80);
                time.day = bcd2bin(time.day);
                time.month = bcd2bin(time.month);
                time.year = bcd2bin(time.year);
                if (century_reg)
                    time.century = bcd2bin(time.century);
            }

            if (!(status_b & (1 << 1)) && (time.hour & (1 << 7)))
                time.hour = ((time.hour & 0x7F) + 12) % 24;

            const auto year = time.year + (century_reg ? time.century * 100 : 2000);
            return lib::timestamp(year, time.month, time.day, time.hour, time.minute, time.second);
        }
    } // namespace

    lib::initgraph::task pit_task
    {
        "arch.rtc",
        lib::initgraph::presched_init_engine,
        lib::initgraph::require {
            ::timers::initialised_stage(),
            acpi::tables_stage()
        },
        [] {
            if (acpi::fadt)
                century_reg = acpi::fadt->century;

            static chrono::rtc rtc { "rtc", get_datetime };
            chrono::register_rtc(rtc);
        }
    };
} // namespace x86_64::rtc
