// Copyright (C) 2024-2025  ilobilo

export module lib:list;

import :bug_on;
import :types;
import std;

export namespace lib
{
    template<typename Type, typename Hook, Hook Type::*Member>
    struct locate_member
    {
        constexpr Hook &operator()(Type &x) { return x.*Member; }
    };

    template<typename Type>
    struct intrusive_list_hook
    {
        template<typename Type1, typename Locate>
        friend class intrusive_list_base;

        private:
        Type *next;
        Type *prev;

        public:
        constexpr intrusive_list_hook() : next { nullptr }, prev { nullptr } { }
    };

    template<typename Type, typename Locate>
    class intrusive_list_base
    {
        private:
        Type *_head;
        Type *_tail;
        std::size_t _size;

        static inline constexpr intrusive_list_hook<Type> &hook(Type *item)
        {
            return Locate { } (*item);
        }

        static inline constexpr intrusive_list_hook<Type> &hook(void *item)
        {
            return hook(static_cast<Type *>(item));
        }

        class iterator
        {
            template<typename Type1, typename Locate1>
            friend class intrusive_list_base;

            private:
            intrusive_list_base *_lst;
            Type *_current;
            constexpr iterator(intrusive_list_base *lst, Type *data) : _lst { lst }, _current { data } { }

            public:
            constexpr Type &operator*() const { return *_current; }
            constexpr Type *operator->() const { return _current; }
            constexpr Type *value() const { return _current; }

            constexpr iterator &operator++()
            {
                _current = hook(_current).next;
                return *this;
            }

            constexpr iterator operator++(int)
            {
                auto ret { *this };
                ++(*this);
                return ret;
            }

            constexpr iterator &operator--()
            {
                _current = hook(_current).prev;
                return *this;
            }

            constexpr iterator operator--(int)
            {
                auto ret { *this };
                --(*this);
                return ret;
            }

            friend constexpr bool operator==(const iterator &lhs, const iterator &rhs)
            {
                return lhs._lst == rhs._lst && lhs._current == rhs._current;
            }

            friend constexpr bool operator!=(const iterator &lhs, const iterator &rhs)
            {
                return !(lhs == rhs);
            }
        };

        public:
        constexpr intrusive_list_base()
            : _head { nullptr }, _tail { nullptr }, _size { 0 } { }

        constexpr intrusive_list_base(const intrusive_list_base &) = delete;
        constexpr intrusive_list_base(intrusive_list_base &&rhs)
            : _head { rhs._head }, _tail { rhs._tail }, _size { rhs._size }
        {
            rhs._head = nullptr;
            rhs._tail = nullptr;
            rhs._size = 0;
        }

        constexpr intrusive_list_base &operator=(const intrusive_list_base &) = delete;
        constexpr intrusive_list_base &operator=(intrusive_list_base &&rhs)
        {
            if (this != &rhs)
            {
                _head = rhs._head;
                _tail = rhs._tail;
                _size = rhs._size;

                rhs._head = nullptr;
                rhs._tail = nullptr;
                rhs._size = 0;
            }
            return *this;
        }

        constexpr iterator insert(Type *x)
        {
            if (_head == nullptr)
            {
                _head = x;
                _tail = x;
                hook(x).next = nullptr;
                hook(x).prev = nullptr;
            }
            else
            {
                hook(_tail).next = x;
                hook(x).prev = _tail;
                hook(x).next = nullptr;
                _tail = x;
            }
            _size++;

            return { this, x };
        }

        constexpr iterator insert_after(iterator pos, Type *x)
        {
            if (!std::is_constant_evaluated())
                bug_on(pos._lst != this);

            const auto next = hook(pos._current).next;
            hook(pos._current).next = x;
            hook(x).prev = pos._current;
            hook(x).next = next;

            if (next)
                hook(next).prev = x;
            else
                _tail = x;

            _size++;
            return { this, x };
        }

        constexpr iterator insert_before(iterator pos, Type *x)
        {
            if (!std::is_constant_evaluated())
                bug_on(pos._lst != this);

            const auto prev = hook(pos._current).prev;
            hook(pos._current).prev = x;
            hook(x).next = pos._current;
            hook(x).prev = prev;

            if (prev)
                hook(prev).next = x;
            else
                _head = x;

            _size++;
            return { this, x };
        }

        constexpr iterator push_back(Type *x)
        {
            return insert(x);
        }

        constexpr iterator push_front(Type *x)
        {
            if (_head == nullptr)
                return insert(x);
            return insert_before({ this, _head }, x);
        }

        constexpr void remove(iterator x)
        {
            if (!std::is_constant_evaluated())
                bug_on(x._lst != this);

            const auto prev = hook(x._current).prev;
            const auto next = hook(x._current).next;

            if (x._current == _head)
                _head = next;
            if (x._current == _tail)
                _tail = prev;

            if (prev)
                hook(prev).next = next;
            if (next)
                hook(next).prev = prev;

            hook(x._current).next = nullptr;
            hook(x._current).prev = nullptr;

            if (!std::is_constant_evaluated())
                bug_on(_size == 0);
            _size--;
        }

        constexpr Type *pop_front()
        {
            auto ret = _head;
            if (ret != nullptr)
                remove({ this, ret });
            return ret;
        }

        constexpr Type *pop_back()
        {
            auto ret = _tail;
            if (ret != nullptr)
                remove({ this, ret });
            return ret;
        }

        constexpr iterator find(Type *x)
        {
            for (auto it = begin(); it != end(); it++)
            {
                if (it._current == x)
                    return it;
            }
            return end();
        }

        constexpr iterator find_if(auto predicate)
        {
            for (auto it = begin(); it != end(); it++)
            {
                if (predicate(*it))
                    return it;
            }
            return end();
        }

        constexpr iterator begin() { return { this, _head }; }
        constexpr iterator end() { return { this, nullptr }; }

        constexpr iterator begin() const { return { this, _head }; }
        constexpr iterator end() const { return { this, nullptr }; }

        constexpr iterator cbegin() { return { this, _head }; }
        constexpr iterator cend() { return { this, nullptr }; }

        constexpr Type *front() { return _head; }
        constexpr Type *back() { return _tail; }

        constexpr std::size_t size() const { return _size; }
        constexpr bool empty() const { return _size == 0; }
    };

    template<typename Type, intrusive_list_hook<Type> Type::*Member>
    using intrusive_list = intrusive_list_base<Type, locate_member<Type, intrusive_list_hook<Type>, Member>>;

    template<typename Type, typename Locate>
    using intrusive_list_locate = intrusive_list_base<Type, Locate>;
} // export namespace lib