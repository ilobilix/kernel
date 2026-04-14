// Copyright (C) 2024-2025  ilobilo

export module lib:locker;

import :bug_on;
import std;

namespace detail
{
    template<typename Type>
    concept is_lock = requires(Type l)
    {
        { l.lock() };
        { l.unlock() };
        { l.is_locked() } -> std::same_as<bool>;
    };

    template<typename Type>
    concept is_rwlock = requires(Type l)
    {
        { l.read_lock() };
        { l.write_lock() };
        { l.read_unlock() };
        { l.write_unlock() };
        { l.is_read_locked() } -> std::same_as<bool>;
        { l.is_write_locked() } -> std::same_as<bool>;
        // { l.upgrade() };
    };

    template<typename Type, typename Lock>
    class locked_ptr;

    template<typename Type, typename Lock, bool Write>
        requires (is_lock<Lock> || is_rwlock<Lock>)
    class locked
    {
        // template<typename, typename ULock, bool>
        //     requires (is_lock<ULock> || is_rwlock<ULock>)
        // friend class locked;

        private:
        Type *_ptr;
        Lock &_lock;

        using ret_type = std::conditional_t<Write, Type, const Type>;

        // locked(Type *ptr, Lock &lock, bool) requires detail::is_rwlock<Lock>
        //     : _ptr { ptr }, _lock { lock } { _lock.upgrade(); }

        public:
        locked(Type *ptr, Lock &lock) : _ptr { ptr }, _lock { lock }
        {
            if constexpr (is_rwlock<Lock>)
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
            // if (_ptr == nullptr)
            //     return;

            if constexpr (is_rwlock<Lock>)
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

        // auto upgrade() & requires (detail::is_rwlock<Lock> && Write == false)
        // {
        //     const auto ptr = _ptr;
        //     _ptr = nullptr;
        //     return locked<Type, Lock, true> { ptr, _lock, true };
        // }
    };

    enum class make_locked_tag_t { };
    inline constexpr make_locked_tag_t make_locked_tag { };

    template<typename Type, typename Lock>
    class storage
    {
        template<typename, typename>
        friend class storage;

        private:
        struct buffer
        {
            Lock _lock;
            alignas(alignof(Type)) std::byte _buffer[sizeof(Type)];

            Type *valueptr() { return std::launder(reinterpret_cast<Type *>(_buffer)); }

            template<typename ...Args>
            constexpr buffer(Args &&...args) : _lock { }
            {
                std::construct_at(reinterpret_cast<Type *>(_buffer), std::forward<Args>(args)...);
            }
            ~buffer() { std::destroy_at(valueptr()); }
        };

        std::shared_ptr<void> _data;
        Type *_ptr;

        template<typename Type1>
        static consteval bool safety()
        {
            using Other = storage<Type1, Lock>;
            static_assert(std::is_base_of_v<Type, Type1>);
            static_assert(alignof(Type) == alignof(Type1));
            static_assert(__builtin_offsetof(buffer, _lock) == __builtin_offsetof(Other::buffer, _lock));
            static_assert(__builtin_offsetof(buffer, _buffer) == __builtin_offsetof(Other::buffer, _buffer));
            return true;
        }

        public:
        constexpr storage() : _data { } { }

        template<typename Type1> requires (safety<Type1>())
        constexpr storage(const storage<Type1, Lock> &rhs) : _data { rhs._data },
            _ptr { static_cast<Type *>(rhs._ptr) } { }

        template<typename Type1> requires (safety<Type1>())
        constexpr storage(storage<Type1, Lock> &&rhs) : _data { std::move(rhs._data) },
            _ptr { static_cast<Type *>(rhs._ptr) } { }

        template<typename ...Args>
        constexpr storage(make_locked_tag_t, Args &&...args)
            : _data { std::make_shared<buffer>(std::forward<Args>(args)...) },
              _ptr { static_cast<buffer *>(_data.get())->valueptr() } { }

        void clear()
        {
            const auto &lock = get_lock();
            if constexpr (is_rwlock<Lock>)
                lib::bug_on(lock.is_read_locked() || lock.is_write_locked());
            else
                lib::bug_on(lock.is_locked());

            _data.reset();
            _ptr = nullptr;
        }

        std::size_t use_count() const { return _data.use_count(); }

        Type *get_data() const
        {
            lib::bug_on(_data == nullptr);
            return _ptr;
        }

        Lock &get_lock() const
        {
            lib::bug_on(_data == nullptr);
            return static_cast<buffer *>(_data.get())->_lock;
        }
    };
} // namespace detail

export namespace lib
{
    template<typename Type, typename Lock>
    class locked_ptr
    {
        template<typename, typename>
        friend class locked_ptr;

        template<typename Type1, typename Lock1, typename ...Args>
        friend locked_ptr<Type1, Lock1> make_locked(Args &&...args);

        private:
        detail::storage<Type, Lock> _storage;

        template<typename ...Args>
        constexpr locked_ptr(detail::make_locked_tag_t, Args &&...args)
            : _storage { detail::make_locked_tag, std::forward<Args>(args)... } { }

        public:
        constexpr locked_ptr() : _storage { } { }

        constexpr locked_ptr(const locked_ptr &rhs)
            : _storage { rhs._storage } { }

        constexpr locked_ptr(locked_ptr &&rhs)
            : _storage { std::move(rhs._storage) } { }

        // type checks in storage::storage()
        template<typename Type1>
        constexpr locked_ptr(const locked_ptr<Type1, Lock> &rhs)
            : _storage { rhs._storage } { }

        template<typename Type1>
        constexpr locked_ptr(locked_ptr<Type1, Lock> &&rhs)
            : _storage { std::move(rhs._storage) } { }

        ~locked_ptr() = default;

        locked_ptr &operator=(const locked_ptr &rhs) = default;
        locked_ptr &operator=(locked_ptr &&rhs) = default;

        void clear() { return _storage.clear(); }
        void reset() { return clear(); }

        std::size_t use_count() const { return _storage.use_count(); }
        bool unique() const { return use_count() == 1; }

        template<typename Self> requires detail::is_lock<Lock>
        [[nodiscard]] auto lock(this Self &&self)
        {
            return detail::locked<Type, Lock, true> {
                std::forward<Self>(self)._storage.get_data(),
                std::forward<Self>(self)._storage.get_lock()
            };
        }

        template<typename Self> requires detail::is_rwlock<Lock>
        [[nodiscard]] auto read_lock(this Self &&self)
        {
            return detail::locked<Type, Lock, false> {
                std::forward<Self>(self)._storage.get_data(),
                std::forward<Self>(self)._storage.get_lock()
            };
        }

        template<typename Self> requires detail::is_rwlock<Lock>
        [[nodiscard]] auto write_lock(this Self &&self)
        {
            return detail::locked<Type, Lock, true> {
                std::forward<Self>(self)._storage.get_data(),
                std::forward<Self>(self)._storage.get_lock()
            };
        }

        // Type *unsafe_nolock_access_data() { return _storage.get_data(); }
        // Lock &unsafe_nolock_access_lock() { return _storage.get_lock(); }
    };

    template<typename Type, typename Lock, typename ...Args>
    inline locked_ptr<Type, Lock> make_locked(Args &&...args)
    {
        return locked_ptr<Type, Lock> {
            detail::make_locked_tag, std::forward<Args>(args)...
        };
    }

    template<typename Type, typename Lock>
    class locker
    {
        template<typename, typename>
        friend class storage;

        private:
        alignas(alignof(Type)) std::byte _buffer[sizeof(Type)];
        Lock _lock;

        Type *valueptr() { return std::launder(reinterpret_cast<Type *>(_buffer)); }

        public:
        using value_type = Type;
        using lock_type = Lock;

        template<typename ...Args>
        constexpr locker(Args &&...args) : _lock { }
        {
            std::construct_at(reinterpret_cast<Type *>(_buffer), std::forward<Args>(args)... );
        }

        ~locker()
        {
            std::destroy_at(valueptr());
        }

        locker(const locker &) = delete;
        locker(locker &&) = delete;

        locker &operator=(const locker &) = delete;
        locker &operator=(locker &&) = delete;

        template<typename Self> requires detail::is_lock<Lock>
        [[nodiscard]] auto lock(this Self &&self)
        {
            return detail::locked<Type, Lock, true> {
                std::forward<Self>(self).valueptr(),
                std::forward<Self>(self)._lock
            };
        }

        bool is_locked() const
        {
            return _lock.is_locked();
        }

        bool is_locked()
        {
            return _lock.is_locked();
        }

        template<typename Self> requires detail::is_rwlock<Lock>
        [[nodiscard]] auto read_lock(this Self &&self)
        {
            return detail::locked<Type, Lock, false> {
                std::forward<Self>(self).valueptr(),
                std::forward<Self>(self)._lock
            };
        }

        bool is_read_locked() const requires detail::is_rwlock<Lock>
        {
            return _lock.is_read_locked();
        }

        template<typename Self> requires detail::is_rwlock<Lock>
        [[nodiscard]] auto write_lock(this Self &&self)
        {
            return detail::locked<Type, Lock, true> {
                std::forward<Self>(self).valueptr(),
                std::forward<Self>(self)._lock
            };
        }

        bool is_write_locked() const requires detail::is_rwlock<Lock>
        {
            return _lock.is_write_locked();
        }
    };
} // export namespace lib