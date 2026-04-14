// Copyright (C) 2024-2026  ilobilo

export module lib:barrier;

namespace lib
{
    inline void rmb()
    {
#if defined(__x86_64__)
        __asm__ __volatile__("" ::: "memory");
#elif defined(__aarch64__)
        __asm__ __volatile__("dmb ishld" ::: "memory");
#else
#  error "unsupported architecture"
#endif
    }

    inline void wmb()
    {
#if defined(__x86_64__)
        __asm__ __volatile__("" ::: "memory");
#elif defined(__aarch64__)
        __asm__ __volatile__("dmb ishst" ::: "memory");
#else
#  error "unsupported architecture"
#endif
    }

    inline void mb()
    {
#if defined(__x86_64__)
        __asm__ __volatile__("mfence" ::: "memory");
#elif defined(__aarch64__)
        __asm__ __volatile__("dmb ish" ::: "memory");
#else
#  error "unsupported architecture"
#endif
    }
} // export namespace lib
