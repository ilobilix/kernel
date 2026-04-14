// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>
#include <cxxabi.h>

module lib;

import system.bin.elf;
import :log;
import cppstd;

namespace lib
{
    void trace(log::level prefix, std::uintptr_t fp, std::uintptr_t ip)
    {
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
            auto [sym, offset, where] = bin::elf::sym::lookup(ip, STT_FUNC);
            bool is_empty = sym == bin::elf::sym::empty;

            int status = -1;
            const char *str = !is_empty ? (abi::__cxa_demangle(sym.name.data(), nullptr, nullptr, &status) ?: sym.name.data()) : "unknown";
            if (status == 1)
                return false;

            log::println(prefix, "[0x{:016X}] ({}) <{}+0x{:X}>", ip, where, str, offset);

            if (status == 0)
                std::free(const_cast<char *>(str));

            return is_empty ?is_empty: (sym.name != "isr_handler" && sym.name != "syscall_handler");
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