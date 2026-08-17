// Copyright (C) 2024-2026  ilobilo

export module lib:buffer;

import :bug_on;
import :math;
import :user;
import std;

export namespace lib
{
    enum class zeroed_t { };
    inline constexpr zeroed_t zeroed { };

    template<typename Type, typename Allocator = std::allocator<Type>>
    class buffer
    {
        private:
        template<typename Self>
        using element_t = std::conditional_t<std::is_const_v<Self>, const Type, Type>;

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

        buffer(std::size_t count, zeroed_t) : buffer { count }
        {
            std::memset(_ptr, 0, count * sizeof(Type));
        }

        buffer(Type *ptr, std::size_t count) : buffer { count }
        {
            std::memcpy(_ptr, tohh(ptr), count * sizeof(Type));
        }

        buffer(buffer &&other) : buffer { } { swap(*this, other); }

        buffer(const buffer &other) : _alloc { other._alloc }
        {
            allocate(other._count);
            std::memcpy(_ptr, other._ptr, _count * sizeof(Type));
        }

        buffer &operator=(const buffer &other)
        {
            if (&other != this)
            {
                allocate(other._count);
                std::memcpy(_ptr, other._ptr, _count * sizeof(Type));
            }
            return *this;
        }

        buffer &operator=(buffer &&other)
        {
            if (&other != this)
                swap(*this, other);
            return *this;
        }

        ~buffer()
        {
            if (_ptr != nullptr)
                _alloc.deallocate(_ptr, _count);
        }

        void allocate(std::size_t count)
        {
            lib::bug_on(count == 0 || _ptr != nullptr);
            _ptr = _alloc.allocate(count);
            _count = count;
        }

        template<typename Self>
        auto span(this Self &self)
        {
            return std::span<element_t<Self>> { self._ptr, self._count };
        }

        template<typename Self>
        auto uspan(this Self &self)
        {
            return lib::maybe_uspan<element_t<Self>>::create(self._ptr, self._count);
        }

        template<typename Self>
        auto byte_span(this Self &self)
        {
            if constexpr (std::is_const_v<Self>)
                return std::as_bytes(self.span());
            else
                return std::as_writable_bytes(self.span());
        }

        template<typename Self>
        auto byte_uspan(this Self &self)
        {
            auto bytes = self.byte_span();
            using byte_t = decltype(bytes)::element_type;
            return lib::maybe_uspan<byte_t>::create(bytes.data(), bytes.size());
        }

        template<typename Self>
        element_t<Self> *data(this Self &self) { return self._ptr; }

        template<typename Self>
        element_t<Self> *virt_data(this Self &self) { return self._ptr; }

        template<typename Self>
        auto phys_data(this Self &self) { return fromhh(self.data()); }

        template<typename Self>
        element_t<Self> &at(this Self &self, std::size_t index)
        {
            lib::bug_on(self._ptr == nullptr || index >= self._count);
            return self._ptr[index];
        }

        template<typename Self>
        element_t<Self> &operator[](this Self &self, std::size_t index)
        {
            return self.at(index);
        }

        template<typename Self>
        element_t<Self> *begin(this Self &self) { return self._ptr; }

        template<typename Self>
        element_t<Self> *end(this Self &self) { return self._ptr + self._count; }

        template<typename Self>
        element_t<Self> &front(this Self &self)
        {
            lib::bug_on(self._ptr == nullptr || self._count == 0);
            return *self.begin();
        }

        template<typename Self>
        element_t<Self> &back(this Self &self)
        {
            lib::bug_on(self._ptr == nullptr || self._count == 0);
            return *(self.end() - 1);
        }

        template<typename Self>
        element_t<Self> &operator*(this Self &self)
        {
            lib::bug_on(self._ptr == nullptr || self._count != 1);
            return self.front();
        }

        template<typename Self>
        element_t<Self> *operator->(this Self &self)
        {
            lib::bug_on(self._ptr == nullptr || self._count != 1);
            return self.data();
        }

        std::size_t size() const { return _count; }
        std::size_t size_bytes() const { return _count * sizeof(Type); }
    };

    using u8buffer = buffer<std::uint8_t>;
    using membuffer = buffer<std::byte>;
} // export namespace lib
