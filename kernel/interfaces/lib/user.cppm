// Copyright (C) 2024-2025  ilobilo

export module lib:user;
import cppstd;

export namespace lib
{
    void copy_to_user(void __user *dest, const void *src, std::size_t len);
    void copy_from_user(void *dest, const void __user *src, std::size_t len);

    void strncpy_from_user(char *dest, const char __user *src, std::size_t len);
    std::size_t strnlen_user(const char __user *str, std::size_t len);

    struct may_be_uptr
    {
        void __user *ptr;

        may_be_uptr(void __user *ptr) : ptr { ptr } { }

        std::uintptr_t address() const
        {
            return reinterpret_cast<std::uintptr_t>(ptr);
        }

        template<typename Type> requires std::is_trivially_copyable_v<Type>
        Type read() const
        {
            Type val;
            lib::copy_from_user(&val, ptr, sizeof(Type));
            return val;
        }

        template<typename Type> requires std::is_trivially_copyable_v<Type>
        void write(const Type &val) const
        {
            lib::copy_to_user(ptr, &val, sizeof(Type));
        }
    };
} // export namespace lib