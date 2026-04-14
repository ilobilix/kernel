// Copyright (C) 2024-2025  ilobilo

export module lib:locker;

import :ensure;
import cppstd;

namespace util
{
    template<typename Type>
    concept is_lock = requires(Type l)
    {
        { l.lock() };
        { l.unlock() };
    };

    template<typename Type>
    concept is_rwlock = requires(Type l)
    {
        { l.read_lock() };
        { l.write_lock() };
        { l.read_unlock() };
        { l.write_unlock() };
    };

    template<typename Type, typename Lock>
    class locked_ptr;

    template<typename Type, typename Lock, bool Write>
        requires (util::is_lock<Lock> || util::is_rwlock<Lock>)
    class locked
    {
        private:
        Type *_ptr;
        Lock &_lock;

        using ret_type = std::conditional_t<Write, Type, const Type>;

        public:
        locked(Type *ptr, Lock &lock) : _ptr { ptr }, _lock { lock }
        {
            if constexpr (util::is_rwlock<Lock>)
            {
                if constexpr (Write)
                    _lock.write_lock();
                else
                    _lock.read_lock();
            }
            else _lock.lock();
        }

        ~locked()
        {
            if constexpr (util::is_rwlock<Lock>)
            {
                if constexpr (Write)
                    _lock.write_unlock();
                else
                    _lock.read_unlock();
            }
            else _lock.unlock();
        }

        locked &operator=(const locked &) = delete;
        locked &operator=(locked &&) = delete;

        locked &operator=(const Type &value)
        {
            *_ptr = value;
            return *this;
        }

        locked &operator=(Type &&value)
        {
            *_ptr = std::move(value);
            return *this;
        }

        [[nodiscard]] ret_type &value() const & { return *_ptr; }
        [[nodiscard]] ret_type &operator*() const & { return *_ptr; }
        [[nodiscard]] ret_type *operator->() const & { return _ptr; }
    };
} // namespace util

export namespace lib
{
    // rust Mutex<> style wrapper
    template<typename Type, typename Lock>
    class locker
    {
        private:
        alignas(alignof(Type)) std::byte _buffer[sizeof(Type)];
        Lock _lock;

        public:
        template<typename ...Args>
        constexpr locker(Args &&...args) : _lock { }
        {
            new(_buffer) Type(std::forward<Args>(args)...);
        }

        ~locker()
        {
            std::launder(reinterpret_cast<Type *>(_buffer))->~Type();
        }

        locker(const locker &) = delete;
        locker(locker &&) = delete;

        locker &operator=(const locker &) = delete;
        locker &operator=(locker &&) = delete;

        template<typename Self> requires util::is_lock<Lock>
        [[nodiscard]] auto lock(this Self &&self)
        {
            return util::locked<Type, Lock, true> {
                std::launder(reinterpret_cast<Type *>(std::forward<Self>(self)._buffer)),
                std::forward<Self>(self)._lock
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto read_lock(this Self &&self)
        {
            return util::locked<Type, Lock, false> {
                std::launder(reinterpret_cast<Type *>(std::forward<Self>(self)._buffer)),
                std::forward<Self>(self)._lock
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto write_lock(this Self &&self)
        {
            return util::locked<Type, Lock, true> {
                std::launder(reinterpret_cast<Type *>(std::forward<Self>(self)._buffer)),
                std::forward<Self>(self)._lock
            };
        }
    };

    template<typename Type, typename Lock>
    class locked_ptr
    {
        template<typename, typename>
        friend class locked_ptr;

        private:
        std::pair<std::shared_ptr<Type>, std::shared_ptr<Lock>> _data;

        public:
        constexpr locked_ptr() : _data { nullptr, nullptr } { }

        constexpr locked_ptr(const std::shared_ptr<Type> &rhs)
            : _data { rhs, std::make_shared<Lock>() } { }

        constexpr locked_ptr(std::shared_ptr<Type> &&rhs)
            : _data { std::move(rhs), std::make_shared<Lock>() } { }

        constexpr locked_ptr(const locked_ptr &rhs)
            : _data { rhs._data } { }

        constexpr locked_ptr(locked_ptr &&rhs)
            : _data { std::move(rhs._data) } { }

        template<typename Type1>
        constexpr locked_ptr(const locked_ptr<Type1, Lock> &rhs)
            : _data { rhs._data.first, rhs._data.second } { }

        template<typename Type1>
        constexpr locked_ptr(locked_ptr<Type1, Lock> &&rhs)
            : _data { std::move(rhs._data.first), std::move(rhs._data.second) } { }

        ~locked_ptr() = default;

        locked_ptr &operator=(const locked_ptr &rhs)
        {
            _data = rhs._data;
            return *this;
        }

        locked_ptr &operator=(locked_ptr &&rhs)
        {
            _data = std::move(rhs._data);
            return *this;
        }

        template<typename Self> requires util::is_lock<Lock>
        [[nodiscard]] auto lock(this Self &&self)
        {
            ensure(std::forward<Self>(self)._data.first != nullptr);
            return util::locked<Type, Lock, true> {
                std::forward<Self>(self)._data.first.get(),
                *std::forward<Self>(self)._data.second
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto read_lock(this Self &&self)
        {
            ensure(std::forward<Self>(self)._data.first != nullptr);
            return util::locked<Type, Lock, false> {
                std::forward<Self>(self)._data.first.get(),
                *std::forward<Self>(self)._data.second
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto write_lock(this Self &&self)
        {
            ensure(std::forward<Self>(self)._data.first != nullptr);
            return util::locked<Type, Lock, true> {
                std::forward<Self>(self)._data.first.get(),
                *std::forward<Self>(self)._data.second
            };
        }
    };

    template<typename Type, typename Lock, typename ...Args>
    inline locked_ptr<Type, Lock> make_locked(Args &&...args)
    {
        return locked_ptr<Type, Lock> { std::make_shared<Type>(std::forward<Args>(args)...) };
    }
} // export namespace lib