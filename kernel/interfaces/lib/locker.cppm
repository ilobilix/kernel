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

    enum class make_locked_tag_t { };
    inline constexpr make_locked_tag_t make_locked_tag { };
} // namespace util

export namespace lib
{
    template<typename Type, typename Lock>
    class locker
    {
        template<typename, typename>
        friend class storage;

        private:
        alignas(alignof(Type)) std::byte _buffer[sizeof(Type)];
        Lock _lock;

        public:
        template<typename ...Args>
        constexpr locker(Args &&...args) : _lock { }
        {
            new(_buffer) Type { std::forward<Args>(args)... };
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
} // export namespace lib

namespace lib
{
    template<typename Type, typename Lock>
    class storage
    {
        template<typename, typename>
        friend class storage;

        private:
        struct dummy { Lock _lock; alignas(alignof(Type)) std::byte _buffer[sizeof(Type)]; };

        std::shared_ptr<void> _data;
        Type *_ptr;

        template<typename AType>
        struct allocator : std::allocator<AType>
        {
            using typename std::allocator<AType>::value_type;
            using typename std::allocator<AType>::pointer;
            using typename std::allocator<AType>::const_pointer;
            using typename std::allocator<AType>::reference;
            using typename std::allocator<AType>::const_reference;
            using typename std::allocator<AType>::size_type;
            using typename std::allocator<AType>::difference_type;

            template<typename U>
            struct rebind { using other = allocator<U>; };

            [[nodiscard]] constexpr AType *allocate(std::size_t count)
            {
                return static_cast<AType *>(::operator new(count * sizeof(AType)));
            }

            constexpr void deallocate(AType *ptr, std::size_t count)
            {
                // !NOTICE: assumes that rebinded type puts dummy at the start of the object (I think)
                // tbh it's 3 am and I can't really think, so this could be perfectly valid (and it does work find)

                auto dummy_ptr = reinterpret_cast<dummy *>(ptr);
                std::destroy_at(reinterpret_cast<Type *>(dummy_ptr->_buffer));
                std::destroy_at(std::addressof(dummy_ptr->_lock));
                ::operator delete(ptr, count * sizeof(AType));
            }
        };

        public:
        constexpr storage() : _data { nullptr } { }
        constexpr storage(std::nullptr_t) : storage { } { }

        template<typename Type1>
            requires (std::is_base_of_v<Type, Type1> && alignof(Type) == alignof(Type1))
        constexpr storage(const storage<Type1, Lock> &rhs) : _data { rhs._data },
            _ptr { static_cast<Type *>(rhs._ptr) }
        {
            // some safety checks. probably unnecessary. as I sad, 3 am
            static_assert(__builtin_offsetof(dummy, _lock) == __builtin_offsetof(storage<Type1, Lock>::dummy, _lock));
            static_assert(__builtin_offsetof(dummy, _buffer) == __builtin_offsetof(storage<Type1, Lock>::dummy, _buffer));
        }

        template<typename Type1>
            requires (std::is_base_of_v<Type, Type1> && alignof(Type) == alignof(Type1))
        constexpr storage(storage<Type1, Lock> &&rhs) : _data { std::move(rhs._data) },
            _ptr { static_cast<Type *>(rhs._ptr) }
        {
            static_assert(__builtin_offsetof(dummy, _lock) == __builtin_offsetof(storage<Type1, Lock>::dummy, _lock));
            static_assert(__builtin_offsetof(dummy, _buffer) == __builtin_offsetof(storage<Type1, Lock>::dummy, _buffer));
        }

        template<typename ...Args>
        constexpr storage(util::make_locked_tag_t, Args &&...args) : _data { }
        {
            _data = std::allocate_shared<dummy>(allocator<dummy> { });

            auto ptr = reinterpret_cast<Type *>(static_cast<dummy *>(_data.get())->_buffer);
            new(ptr) Type { std::forward<Args>(args)... };
            _ptr = std::launder(ptr);

            auto lock = std::addressof(static_cast<dummy *>(_data.get())->_lock);
            new(lock) Lock { };
        }

        Type *get_data() const
        {
            lib::ensure(_data != nullptr);
            return _ptr;
        }

        Lock &get_lock() const
        {
            lib::ensure(_data != nullptr);
            return std::launder(reinterpret_cast<dummy *>(_data.get()))->_lock;
        }
    };
} // namespace lib

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
        lib::storage<Type, Lock> _storage;

        template<typename ...Args>
        constexpr locked_ptr(util::make_locked_tag_t, Args &&...args)
            : _storage { util::make_locked_tag, std::forward<Args>(args)... } { }

        public:
        constexpr locked_ptr() : _storage { } { }

        // type checks in storage::storage()
        template<typename Type1>
        constexpr locked_ptr(const locked_ptr<Type1, Lock> &rhs)
            : _storage { rhs._storage } { }

        template<typename Type1>
        constexpr locked_ptr(locked_ptr<Type1, Lock> &&rhs)
            : _storage { std::move(rhs._storage) } { }

        ~locked_ptr() = default;

        locked_ptr &operator=(const locked_ptr &rhs)
        {
            _storage = rhs._storage;
            return *this;
        }

        locked_ptr &operator=(locked_ptr &&rhs)
        {
            _storage = std::move(rhs._storage);
            return *this;
        }

        template<typename Self> requires util::is_lock<Lock>
        [[nodiscard]] auto lock(this Self &&self)
        {
            return util::locked<Type, Lock, true> {
                std::forward<Self>(self)._storage.get_data(),
                std::forward<Self>(self)._storage.get_lock()
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto read_lock(this Self &&self)
        {
            return util::locked<Type, Lock, false> {
                std::forward<Self>(self)._storage.get_data(),
                std::forward<Self>(self)._storage.get_lock()
            };
        }

        template<typename Self> requires util::is_rwlock<Lock>
        [[nodiscard]] auto write_lock(this Self &&self)
        {
            return util::locked<Type, Lock, true> {
                std::forward<Self>(self)._storage.get_data(),
                std::forward<Self>(self)._storage.get_lock()
            };
        }
    };

    template<typename Type, typename Lock, typename ...Args>
    inline locked_ptr<Type, Lock> make_locked(Args &&...args)
    {
        return locked_ptr<Type, Lock> {
            util::make_locked_tag, std::forward<Args>(args)...
        };
    }
} // export namespace lib