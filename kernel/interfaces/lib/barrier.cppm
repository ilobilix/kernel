// Copyright (C) 2024-2026  ilobilo

export module lib:barrier;

export namespace lib
{
    inline void cmb()
    {
        asm volatile ("" ::: "memory");
    }

    inline void rmb()
    {
#if defined(__x86_64__)
        asm volatile ("" ::: "memory");
#elif defined(__aarch64__)
        asm volatile ("dmb ishld" ::: "memory");
#else
#  error "unsupported architecture"
#endif
    }

    inline void wmb()
    {
#if defined(__x86_64__)
        asm volatile ("" ::: "memory");
#elif defined(__aarch64__)
        asm volatile ("dmb ishst" ::: "memory");
#else
#  error "unsupported architecture"
#endif
    }

    inline void mb()
    {
#if defined(__x86_64__)
        asm volatile ("mfence" ::: "memory");
#elif defined(__aarch64__)
        asm volatile ("dmb ish" ::: "memory");
#else
#  error "unsupported architecture"
#endif
    }
} // export namespace lib
