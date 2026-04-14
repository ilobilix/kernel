// Copyright (C) 2024  ilobilo

export module lib:lazy_init;
import std;

export namespace lib
{
    template<typename Type>
    class lazy_init
    {
        private:
        std::byte _data[sizeof(Type)];
        bool _initialised;

        public:
        constexpr lazy_init() : _data { }, _initialised { false } { }
        ~lazy_init()
        {
            if (_initialised)
                std::destroy_at(reinterpret_cast<Type *>(_data));
        }

        template<typename ...Args>
        constexpr bool initialise(Args &&...args)
        {
            if (_initialised)
                return false;

            new (std::addressof(_data)) Type { std::forward<Args>(args)... };
            return _initialised = true;
        }

        constexpr bool is_initialised() const
        {
            return _initialised;
        }

        constexpr Type *operator->()
        {
            return reinterpret_cast<Type *>(_data);
        }

        constexpr Type &operator*()
        {
            return *reinterpret_cast<Type *>(_data);
        }
    };
} // export namespace lib