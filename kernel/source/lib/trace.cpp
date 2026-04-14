// Copyright (C) 2024-2025  ilobilo

module;

#include <elf.h>
#include <cxxabi.h>

module lib;

import system.bin.elf;
import :log;
import std;

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

        auto print = [prefix](std::uintptr_t ip)
        {
            auto [sym, offset, where] = bin::elf::lookup(ip, STT_FUNC);
            if (sym == bin::elf::empty_symbol)
                return false;

            int status = 1;
            const char *str = abi::__cxa_demangle(sym.name.data(), nullptr, nullptr, &status) ?: sym.name.data();
            if (status == 1)
                return false;

            log::println(prefix, "[0x{:016X}] ({}) <{}+0x{:X}>", ip, where, str, offset);

            if (status == 0)
                std::free(const_cast<char *>(str));

            return sym.name != "isr_handler" && sym.name != "syscall_handler";
        };

        log::println(prefix, "stack trace:");
        if (ip != 0)
            print(ip);

        for (std::size_t i = 0; i < 10; i++)
        {
            if (!frame || !frame->ip)
                break;

            if (!print(frame->ip))
                break;

            frame = frame->next;
        }
    }
} // namespace lib