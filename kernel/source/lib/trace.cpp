// Copyright (C) 2024-2025  ilobilo

module;

#include <cxxabi.h>

module lib;

import system.bin.elf;
import :log;
import cppstd;

namespace lib
{
    void trace(log::level prefix, std::uintptr_t fp, std::uintptr_t ip)
    {
        if (!bin::elf::sym::kernel_loaded())
            return;

        if (fp == 0)
            fp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));

        struct stackframe
        {
            stackframe *next;
            std::uintptr_t ip;
        };
        auto frame = reinterpret_cast<stackframe *>(fp);
        if (!frame)
            return;

        auto print = [prefix](std::uintptr_t ip) -> bool
        {
            std::array<char, KSYM_NAME_LEN> namebuf { "unknown" };
            auto ret = bin::elf::sym::lookup(ip, namebuf);
            bool is_empty = !ret.has_value();

            std::string_view str = !is_empty ? std::string_view { namebuf.data(), std::strnlen(namebuf.data(), KSYM_NAME_LEN) } : std::string_view { "unknown" };

            auto [offset, where] = ret.value_or(bin::elf::sym::lookup_result { 0, "unknown" });
            log::println(prefix, "[0x{:016X}] ({}) <{}+0x{:X}>", ip, where, str, offset);

            return is_empty ? false : (str != "isr_handler"sv && str != "syscall_handler"sv);
        };

        log::println(prefix, "stack trace:");
        if (ip != 0)
            print(ip);

        for (std::size_t i = 0; i < 20; i++)
        {
            if (!frame || !frame->ip)
                break;

            if (!print(frame->ip))
                break;

            frame = frame->next;
        }
    }
} // namespace lib