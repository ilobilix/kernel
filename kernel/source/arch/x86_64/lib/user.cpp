// Copyright (C) 2024-2026  ilobilo

module lib;

import system.sched;
import system.cpu;
import std;

extern "C" int user_copy_safe(void *fault_frame, void *dst, const void *src, std::size_t len);
extern "C" int user_fill_safe(void *fault_frame, void *dst, int val, std::size_t len);
extern "C" std::ssize_t user_strnlen_safe(void *fault_frame, const char *str, std::size_t max);

namespace lib::impl
{
    bool copy_to_user(void __user *dest, const void *src, std::size_t len)
    {
        cpu::smap::disable();
        auto frame = &sched::current_thread()->fault_frame;
        const auto ret = user_copy_safe(frame, remove_user_cast<void>(dest), src, len);
        cpu::smap::enable();
        return ret == 0;
    }

    bool copy_from_user(void *dest, const void __user *src, std::size_t len)
    {
        cpu::smap::disable();
        auto frame = &sched::current_thread()->fault_frame;
        const auto ret = user_copy_safe(frame, dest, remove_user_cast<const void>(src), len);
        cpu::smap::enable();
        return ret == 0;
    }

    bool fill_user(void __user *dest, int value, std::size_t len)
    {
        cpu::smap::disable();
        auto frame = &sched::current_thread()->fault_frame;
        const auto ret = user_fill_safe(frame, remove_user_cast<void>(dest), value, len);
        cpu::smap::enable();
        return ret == 0;
    }

    std::ssize_t strnlen_user(const char __user *str, std::size_t len)
    {
        cpu::smap::disable();
        auto frame = &sched::current_thread()->fault_frame;
        const auto ret = user_strnlen_safe(frame, remove_user_cast<const char>(str), len);
        cpu::smap::enable();
        return ret;
    }
} // namespace lib::impl
