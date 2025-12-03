// Copyright (C) 2024-2025  ilobilo

import arch;
import lib;
import std;

extern "C"
{
    static constexpr std::uint64_t atexit_max_funcs = 128;

    __extension__ typedef int __guard __attribute__((mode(__DI__)));

    struct {
        void (*destructor_func)(void *);
        void *obj_ptr;
        void *dso_handle;
    }  __atexit_funcs[atexit_max_funcs];

    unsigned __atexit_func_count = 0;
    void *__dso_handle = nullptr;

    int __cxa_atexit(void (*func)(void *), void *objptr, void *dso)
    {
        if (__atexit_func_count >= atexit_max_funcs)
            return -1;

        __atexit_funcs[__atexit_func_count].destructor_func = func;
        __atexit_funcs[__atexit_func_count].obj_ptr = objptr;
        __atexit_funcs[__atexit_func_count].dso_handle = dso;
        __atexit_func_count++;

        return 0;
    };

    void __cxa_finalize(void *func)
    {
        auto i = __atexit_func_count;
        if (func == nullptr)
        {
            while (i--)
            {
                if (__atexit_funcs[i].destructor_func)
                    (*__atexit_funcs[i].destructor_func)(__atexit_funcs[i].obj_ptr);
            }
            return;
        }

        while (i--)
        {
            if (__atexit_funcs[i].destructor_func == func)
            {
                (*__atexit_funcs[i].destructor_func)(__atexit_funcs[i].obj_ptr);
                __atexit_funcs[i].destructor_func = 0;
            };
        };
    };

    void __cxa_pure_virtual()
    {
        lib::panic("__cxa_pure_virtual()");
        std::unreachable();
    }

    int __cxa_guard_acquire(__guard *guard)
    {
        if ((*guard) & 0x0001)
            return 0;
        if ((*guard) & 0x0100)
            lib::panic("__cxa_guard_acquire()");

        *guard |= 0x0100;
        return 1;
    }

    void __cxa_guard_release(__guard *guard)
    {
        *guard |= 0x0001;
    }

    void __cxa_guard_abort(__guard *guard)
    {
        lib::panic("__cxa_guard_abort(0x{:X})", reinterpret_cast<std::uintptr_t>(guard));
        std::unreachable();
    }

    std::uintptr_t __stack_chk_guard = 0x595E9FBD94FDA766;
    [[noreturn]] void __stack_chk_fail()
    {
        lib::panic("Stack smashing detected");
        std::unreachable();
    }

    [[gnu::noreturn]] void abort() noexcept
    {
        lib::panic("std::abort()");
        std::unreachable();
    }
} // extern "C"

namespace std
{
    [[maybe_unused]]
    const nothrow_t nothrow;

    [[gnu::noreturn]] void terminate() noexcept
    {
        lib::panic("std::terminate()");
        std::unreachable();
    }

    std::size_t _Hash_bytes(const void *key, std::size_t len, std::size_t seed)
    {
        return lib::hash::murmur3_64(key, len, seed);
    }

    // std::size_t _Fnv_hash_bytes(const void *key, std::size_t len, std::size_t seed)
    // {
    //     return FNV1a(key, len, seed);
    // }

#if __cplusplus < 202400L
    bad_alloc::~bad_alloc() throw() { }
    const char *bad_alloc::what() const throw() { return "bad_alloc"; }
#else
    bool is_debugger_present() noexcept
    {
        return false;
    }

    void breakpoint() noexcept
    {
        __builtin_debugtrap();
    }

    void breakpoint_if_debugging() noexcept
    {
        if (is_debugger_present()) [[unlikely]]
            breakpoint();
    }
#endif
} // namespace std

[[nodiscard]] void *operator new(std::size_t size) { return lib::alloc(size); }
[[nodiscard]] void *operator new(std::size_t size, const std::nothrow_t &) noexcept { return lib::alloc(size); }

[[nodiscard]] void *operator new(std::size_t size, std::align_val_t) { return lib::alloc(size); }
[[nodiscard]] void *operator new(std::size_t size, std::align_val_t, const std::nothrow_t &) noexcept { return lib::alloc(size); }

[[nodiscard]] void *operator new[](std::size_t size) { return lib::alloc(size); }
[[nodiscard]] void *operator new[](std::size_t size, const std::nothrow_t &) noexcept { return lib::alloc(size); }

[[nodiscard]] void *operator new[](std::size_t size, std::align_val_t) { return lib::alloc(size); }
[[nodiscard]] void *operator new[](std::size_t size, std::align_val_t, const std::nothrow_t &) noexcept { return lib::alloc(size); }

void operator delete(void *ptr) noexcept { lib::free(ptr); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete(void *ptr, std::align_val_t) noexcept { lib::free(ptr); }
void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete(void *ptr, std::size_t) noexcept { lib::free(ptr); }
void operator delete(void *ptr, std::size_t, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept { lib::free(ptr); }
void operator delete(void *ptr, std::size_t, std::align_val_t, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete[](void *ptr) noexcept { lib::free(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete[](void *ptr, std::align_val_t) noexcept { lib::free(ptr); }
void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete[](void *ptr, std::size_t) noexcept { lib::free(ptr); }
void operator delete[](void *ptr, std::size_t, const std::nothrow_t &) noexcept { lib::free(ptr); }

void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept { lib::free(ptr); }
void operator delete[](void *ptr, std::size_t, std::align_val_t, const std::nothrow_t &) noexcept { lib::free(ptr); }
