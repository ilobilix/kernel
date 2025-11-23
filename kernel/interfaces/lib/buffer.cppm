// Copyright (C) 2024-2025  ilobilo

export module lib:buffer;

import :bug_on;
import :math;
import :user;
import cppstd;

export namespace lib
{
    template<typename Type, typename Allocator = std::allocator<Type>>
    class buffer
    {
        private:
        Allocator _alloc;
        Type *_ptr;
        std::size_t _count;

        public:
        friend void swap(buffer &lhs, buffer &rhs)
        {
            using std::swap;
            swap(lhs._alloc, rhs._alloc);
            swap(lhs._ptr, rhs._ptr);
            swap(lhs._count, rhs._count);
        }

        buffer()
            : _alloc { }, _ptr { nullptr }, _count { 0 } { }
        buffer(std::size_t count)
            : _alloc { }, _ptr { _alloc.allocate(count) }, _count { count } { }

        buffer(Type *ptr, std::size_t count) : buffer { count }
        {
            std::memcpy(_ptr, tohh(ptr), count * sizeof(Type));
        }

        buffer(buffer &&other) noexcept : buffer { } { swap(*this, other); }
        buffer &operator=(buffer &&other) noexcept { swap(*this, other); return *this; }

        buffer(const buffer &other) = delete;
        buffer &operator=(const buffer &other) = delete;

        ~buffer()
        {
            if (_ptr != nullptr)
                _alloc.deallocate(_ptr, _count);
        }

        void allocate(std::size_t count)
        {
            lib::bug_on(count == 0);

            if (_ptr != nullptr)
                _alloc.deallocate(_ptr, _count);

            _ptr = _alloc.allocate(count);
            _count = count;
        }

        template<typename Self>
        auto span(this Self &&self)
        {
            return std::span<Type> {
                std::forward<Self>(self)._ptr,
                std::forward<Self>(self)._count
            };
        }

        template<typename Self>
        auto maybe_uspan(this Self &&self)
        {
            return lib::maybe_uspan<Type>::create(
                std::forward<Self>(self)._ptr,
                std::forward<Self>(self)._count
            ).value();
        }

        template<typename Self>
        auto data(this Self &&self) { return std::forward<Self>(self)._ptr; }

        template<typename Self>
        auto virt_data(this Self &&self) { return std::forward<Self>(self)._ptr; }

        template<typename Self>
        auto phys_data(this Self &&self) { return fromhh(std::forward<Self>(self)._ptr); }

        template<typename Self>
        auto at(this Self &&self, std::size_t index)
        {
            lib::bug_on(self._ptr == nullptr || index >= self._count);
            return std::forward<Self>(self)._ptr[index];
        }

        template<typename Self>
        auto begin(this Self &&self)
        {
            lib::bug_on(self._ptr == nullptr || self.count == 0);
            return std::forward<Self>(self)._ptr;
        }

        template<typename Self>
        auto end(this Self &&self)
        {
            lib::bug_on(self._ptr == nullptr || self.count == 0);
            return std::forward<Self>(self)._ptr + (self.count - 1);
        }

        std::size_t size() const { return _count; }
        std::size_t size_bytes() const { return _count * sizeof(Type); }
    };

    using u8buffer = buffer<std::uint8_t>;
    using membuffer = buffer<std::byte>;
} // export namespace lib