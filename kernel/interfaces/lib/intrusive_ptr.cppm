// Copyright (C) 2024-2026  ilobilo

export module lib:intrusive_ptr;

import :bug_on;
import :types;
import std;

export namespace lib
{
    struct intrusive_ptr_hook
    {
        template<typename Type, auto Hook>
            requires std::is_member_object_pointer_v<decltype(Hook)>
        friend class intrusive_ptr;

        private:
        std::atomic<std::uint32_t> _count;

        public:
        constexpr intrusive_ptr_hook() : _count { 0 } { }

        constexpr intrusive_ptr_hook(const intrusive_ptr_hook &) = delete;
        constexpr intrusive_ptr_hook(intrusive_ptr_hook &&) = delete;
    };

    template<typename Type, auto Hook>
        requires std::is_member_object_pointer_v<decltype(Hook)>
    class intrusive_ptr
    {
        private:
        Type *_ptr;

        inline constexpr intrusive_ptr_hook &hook() const
        {
            return _ptr->*Hook;
        }

        constexpr void ref()
        {
            if (_ptr)
                hook()._count.fetch_add(1, std::memory_order_relaxed);
        }

        constexpr void unref()
        {
            if (_ptr)
            {
                if (hook()._count.fetch_sub(1, std::memory_order_release) == 1)
                {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    delete _ptr;
                }
                _ptr = nullptr;
            }
        }

        public:
        using value_type = Type;

        constexpr intrusive_ptr() : _ptr { nullptr } { }
        constexpr intrusive_ptr(std::nullptr_t) : _ptr { nullptr } { }

        explicit constexpr intrusive_ptr(Type *ptr)
            : _ptr { ptr } { ref(); }

        template<typename UType>
            requires std::is_convertible_v<UType *, Type *>
        explicit constexpr intrusive_ptr(UType *ptr)
            : _ptr { static_cast<Type *>(ptr) } { ref(); }

        constexpr intrusive_ptr(const intrusive_ptr &rhs)
            : _ptr { rhs._ptr } { ref(); }
        constexpr intrusive_ptr(intrusive_ptr &&rhs)
            : _ptr { rhs._ptr } { rhs._ptr = nullptr; }

        template<typename UType, auto UHook>
            requires std::is_convertible_v<UType *, Type *>
        constexpr intrusive_ptr(const intrusive_ptr<UType, UHook> &rhs)
            : _ptr { static_cast<Type *>(rhs._ptr) } { ref(); }

        template<typename UType, auto UHook>
            requires std::is_convertible_v<UType *, Type *>
        constexpr intrusive_ptr(intrusive_ptr<UType, UHook> &&rhs)
            : _ptr { static_cast<Type *>(rhs._ptr) } { rhs._ptr = nullptr; }

        constexpr intrusive_ptr &operator=(Type *ptr)
        {
            if (_ptr != ptr)
            {
                intrusive_ptr tmp { ptr };
                swap(tmp);
            }
            return *this;
        }

        template<typename UType>
            requires std::is_convertible_v<UType *, Type *>
        constexpr intrusive_ptr &operator=(UType *ptr)
        {
            if (_ptr != ptr)
            {
                intrusive_ptr tmp { ptr };
                swap(tmp);
            }
            return *this;
        }

        constexpr intrusive_ptr &operator=(const intrusive_ptr &rhs)
        {
            if (_ptr != rhs._ptr)
            {
                intrusive_ptr tmp { rhs };
                swap(tmp);
            }
            return *this;
        }

        constexpr intrusive_ptr &operator=(intrusive_ptr &&rhs)
        {
            if (_ptr != rhs._ptr)
            {
                intrusive_ptr tmp { std::move(rhs) };
                swap(tmp);
            }
            return *this;
        }

        template<typename UType, auto UHook>
            requires std::is_convertible_v<UType *, Type *>
        constexpr intrusive_ptr &operator=(const intrusive_ptr<UType, UHook> &rhs)
        {
            if (_ptr != rhs._ptr)
            {
                intrusive_ptr tmp { rhs };
                swap(tmp);
            }
            return *this;
        }

        template<typename UType, auto UHook>
            requires std::is_convertible_v<UType *, Type *>
        constexpr intrusive_ptr &operator=(intrusive_ptr<UType, UHook> &&rhs)
        {
            if (_ptr != rhs._ptr)
            {
                intrusive_ptr tmp { std::move(rhs) };
                swap(tmp);
            }
            return *this;
        }

        constexpr ~intrusive_ptr() { unref(); }

        constexpr Type *get() const { return _ptr; }
        constexpr Type &operator*() const { return *_ptr; }
        constexpr Type *operator->() const { return _ptr; }

        explicit constexpr operator bool() const { return _ptr != nullptr; }

        auto use_count() const
        {
            if (!_ptr)
                return 0;
            return hook()._count.load(std::memory_order_relaxed);
        }

        void swap(intrusive_ptr &other)
        {
            std::swap(_ptr, other._ptr);
        }
    };

    template<typename Type1, auto Hook1, typename Type2, auto Hook2>
    constexpr bool operator==(const intrusive_ptr<Type1, Hook1> &lhs, const intrusive_ptr<Type2, Hook2> &rhs) noexcept
    {
        return lhs.get() == rhs.get();
    }

    template<typename Type, auto Hook>
    constexpr bool operator==(const intrusive_ptr<Type, Hook> &lhs, std::nullptr_t) noexcept
    {
        return lhs.get() == nullptr;
    }

    template<typename Type1, auto Hook1, typename Type2, auto Hook2>
    constexpr auto operator<=>(const intrusive_ptr<Type1, Hook1> &lhs, const intrusive_ptr<Type2, Hook2> &rhs) noexcept
    {
        return lhs.get() <=> rhs.get();
    }

    template<typename Type, auto Hook>
    constexpr auto operator<=>(const intrusive_ptr<Type, Hook> &lhs, std::nullptr_t) noexcept
    {
        return lhs.get() <=> static_cast<Type *>(nullptr);
    }

    template<typename Type, auto Hook, typename ...Args>
    constexpr intrusive_ptr<Type, Hook> make_intrusive(Args &&...args)
    {
        return intrusive_ptr<Type, Hook> { new Type { std::forward<Args>(args)... } };
    }
} // export namespace lib
