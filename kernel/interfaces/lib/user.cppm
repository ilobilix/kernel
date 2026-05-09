// Copyright (C) 2024-2026  ilobilo

export module lib:user;

import :bug_on;
import :types;
import std;

namespace lib::impl
{
    bool copy_to_user(void __user *dest, const void *src, std::size_t len);
    bool copy_from_user(void *dest, const void __user *src, std::size_t len);
    bool fill_user(void __user *dest, int value, std::size_t len);
    std::ssize_t strnlen_user(const char __user *str, std::size_t len);
} // namespace lib::impl

export namespace lib
{
    enum class address_space { invalid, user, kernel };
    address_space classify_address(std::uintptr_t addr, std::size_t len);

    bool copy_to_user(void __user *dest, const void *src, std::size_t len);
    bool copy_from_user(void *dest, const void __user *src, std::size_t len);
    bool fill_user(void __user *dest, int value, std::size_t len);
    std::ssize_t strnlen_user(const char __user *str, std::size_t len);

    template<typename Type, typename Arg> requires (!has_address_space_v<Type>)
    inline Type *remove_user_cast(Arg __user *ptr)
    {
        return (__force Type *)(ptr);
    }

    template<typename Type, typename Arg> requires (!has_address_space_v<Type>)
    inline Type __user *add_user_cast(Arg *ptr)
    {
        return (__force Type __user *)(ptr);
    }

    template<typename Type>
    class maybe_uspan
    {
        private:
        std::span<Type> _span;
        bool _is_user;

        maybe_uspan(bool is_user, std::span<Type> span)
            : _span { span }, _is_user { is_user } { }

        public:
        maybe_uspan() : _span { }, _is_user { false } { }

        static std::optional<maybe_uspan<Type>> create(Type __user *ptr, std::size_t len)
        {
            if (len == 0)
                return maybe_uspan<Type> { true, std::span<Type> { } };

            const auto space = classify_address(reinterpret_cast<std::uintptr_t>(ptr), len * sizeof(Type));
            if (space != address_space::user)
                return std::nullopt;
            return maybe_uspan<Type> { true, std::span<Type>(remove_user_cast<Type>(ptr), len) };
        }

        static std::optional<maybe_uspan<Type>> create(Type *ptr, std::size_t len)
        {
            if (len == 0)
                return maybe_uspan<Type> { false, std::span<Type> { } };

            const auto space = classify_address(reinterpret_cast<std::uintptr_t>(ptr), len * sizeof(Type));
            if (space != address_space::kernel)
                return std::nullopt;
            return maybe_uspan<Type> { false, std::span<Type>(ptr, len) };
        }

        static std::optional<maybe_uspan<Type>> create(void __user *ptr, std::size_t len)
            requires (!std::same_as<Type, void>)
        {
            return create(reinterpret_cast<Type __user *>(ptr), len);
        }

        static std::optional<maybe_uspan<Type>> create(const void __user *ptr, std::size_t len)
            requires (!std::same_as<Type, void>)
        {
            return create(const_cast<void __user *>(ptr), len);
        }

        std::span<Type> span() const { return _span; }
        bool is_user() const { return _is_user; }

        bool copy_to(std::span<Type> dest) const
        {
            if (dest.size_bytes() > _span.size_bytes())
                return false;

            auto destptr = const_cast<std::remove_const_t<Type> *>(dest.data());
            if (_is_user)
            {
                return impl::copy_from_user(
                    destptr,
                    add_user_cast<void>(_span.data()),
                    dest.size_bytes()
                );
            }

            std::memcpy(destptr, _span.data(), dest.size_bytes());
            return true;
        }

        bool copy_to(Type *dest) const
        {
            return copy_to(std::span<Type> { dest, _span.size() });
        }

        bool copy_from(std::span<const Type> src) const
        {
            if (src.size_bytes() > _span.size_bytes())
                return false;

            if (_is_user)
            {
                return impl::copy_to_user(
                    add_user_cast<void>(_span.data()),
                    src.data(),
                    src.size_bytes()
                );
            }

            std::memcpy(_span.data(), src.data(), src.size_bytes());
            return true;
        }

        bool copy_from(const Type *src) const
        {
            return copy_from(std::span<const Type> { src, _span.size() });
        }

        bool fill(std::uint8_t value, std::size_t count) const
        {
            if (count > _span.size_bytes())
                return false;

            if (_is_user)
            {
                return impl::fill_user(
                    add_user_cast<void>(_span.data()),
                    static_cast<int>(value),
                    count * sizeof(Type)
                );
            }

            std::memset(
                reinterpret_cast<void *>(_span.data()),
                static_cast<int>(value),
                count * sizeof(Type)
            );
            return true;
        }

        bool fill(std::uint8_t value) const
        {
            return fill(value, _span.size_bytes());
        }

        std::size_t size() const { return _span.size(); }
        std::size_t size_bytes() const { return _span.size_bytes(); }

        bool empty() const { return _span.empty(); }

        maybe_uspan<Type> subspan(std::size_t offset, std::size_t length) const
        {
            lib::bug_on(offset + length > _span.size());
            return maybe_uspan<Type> { _is_user, _span.subspan(offset, length) };
        }

        maybe_uspan<Type> subspan(std::size_t offset) const
        {
            lib::bug_on(offset > _span.size());
            return maybe_uspan<Type> { _is_user, _span.subspan(offset) };
        }
    };

    struct uptr_or_addr
    {
        private:
        void __user *ptr;

        public:
        uptr_or_addr(void __user *ptr) : ptr { ptr } { }

        uptr_or_addr(const uptr_or_addr &) = default;
        uptr_or_addr(uptr_or_addr &&) = default;

        uptr_or_addr &operator=(const uptr_or_addr &) = default;
        uptr_or_addr &operator=(uptr_or_addr &&) = default;

        std::uintptr_t address() const
        {
            return reinterpret_cast<std::uintptr_t>(ptr);
        }

        template<typename Type> requires std::is_trivially_copyable_v<Type>
        bool read(Type &val) const
        {
            return lib::copy_from_user(&val, ptr, sizeof(Type));
        }

        template<typename Type> requires std::is_trivially_copyable_v<Type>
        bool write(const Type &val) const
        {
            return lib::copy_to_user(ptr, &val, sizeof(Type));
        }
    };
} // export namespace lib
