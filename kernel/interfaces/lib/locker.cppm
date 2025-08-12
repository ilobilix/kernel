// Copyright (C) 2024-2025  ilobilo

export module lib:locker;
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
} // namespace util

export namespace lib
{
    // rust Mutex<> style wrapper
    template<typename Type, typename Lock>
        requires (util::is_lock<Lock> || util::is_rwlock<Lock>)
    class locker
    {
        private:
        Type _value;
        Lock _lock;

        template<bool Write>
        class locked
        {
            friend class locker;

            private:
            Type *_ptr;
            Lock &_lock;

            using ret_type = std::conditional_t<Write, Type, const Type>;

            protected:
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

            public:
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

        public:
        template<typename ...Args>
        constexpr locker(Args &&...args) : _value { std::forward<Args>(args)... }, _lock { } { }

        ~locker() = default;

        locker(const locker &) = delete;
        locker(locker &&) = delete;

        locker &operator=(const locker &) = delete;
        locker &operator=(locker &&) = delete;

        template<typename Self> requires util::is_lock<Lock>
        [[nodiscard]] auto lock(this Self &&self)
        {
            return locked<true> {
                std::addressof(std::forward<Self>(self)._value),
                std::forward<Self>(self)._lock
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto read_lock(this Self &&self)
        {
            return locked<false> {
                std::addressof(std::forward<Self>(self)._value),
                std::forward<Self>(self)._lock
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto write_lock(this Self &&self)
        {
            return locked<true> {
                std::addressof(std::forward<Self>(self)._value),
                std::forward<Self>(self)._lock
            };
        }
    };
} // export namespace lib