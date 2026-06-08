// Copyright (C) 2024-2026  ilobilo

module;

#include <demangler.hpp>

module lib;

import system.bin.elf;
import :log;
import std;

namespace lib
{
    namespace
    {
        char demangle_buffer[4096];
    } // namespace

    void trace(log::level prefix, std::uintptr_t fp, std::uintptr_t ip)
    {
        if (fp == 0 || fp & 7)
            fp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));

        struct stackframe
        {
            stackframe *next;
            std::uintptr_t ip;
        };
        auto frame = reinterpret_cast<stackframe *>(fp);
        if (!frame)
            return;

        auto print = [prefix, &frame](std::uintptr_t ip) {
            std::array<char, KSYM_NAME_LEN> namebuf { "unknown" };
            auto ret = bin::elf::sym::lookup(ip, namebuf);

            std::string_view str { "unknown" };
            if (ret.has_value())
            {
                const bool ret = absl::debugging_internal::Demangle(
                    namebuf.data(), demangle_buffer, sizeof(demangle_buffer)
                );
                if (ret)
                {
                    str = std::string_view {
                        demangle_buffer, std::strnlen(demangle_buffer, sizeof(demangle_buffer))
                    };
                }
                else
                {
                    str = std::string_view {
                        namebuf.data(), std::strnlen(namebuf.data(), KSYM_NAME_LEN)
                    };
                }
            }

            auto [offset, where] = ret.value_or(bin::elf::sym::lookup_result { 0, "unknown" });

            const bool is_last = !frame || !frame->ip;
            if (!is_last || where != "unknown")
                lib::println(prefix, "[0x{:016X}] ({}) <{}+0x{:X}>", ip, where, str, offset);
        };

        lib::println(prefix, "stack trace:");
        if (ip != 0 && lib::ishh(ip))
            print(ip);

        for (std::size_t i = 0; i < 20; i++)
        {
            if (!frame || !frame->ip)
                break;

            ip = frame->ip;
            frame = frame->next;

            print(ip);
        }
    }
} // namespace lib
