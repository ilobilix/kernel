// Copyright (C) 2024-2026  ilobilo

export module lib:list;

import :bug_on;
import std;

export namespace lib
{
    template<typename Type>
    class list
    {
        private:
        struct node
        {
            alignas(alignof(Type)) std::byte data[sizeof(Type)];

            template<typename ...Args>
            node(Args &&...args)
            {
                std::construct_at(reinterpret_cast<Type *>(data), std::forward<Args>(args)...);
            }

            ~node()
            {
                std::destroy_at(std::launder(reinterpret_cast<Type *>(data)));
            }

            Type *get_data()
            {
                return std::launder(reinterpret_cast<Type *>(data));
            }

            node *next = nullptr;
            node *prev = nullptr;
        };

        node *_head;
        node *_tail;
        std::size_t _size;

        template<typename VType>
        class iterator_base
        {
            template<typename>
            friend class iterator_base;

            template<typename>
            friend class list;

            private:
            const list *_lst;
            node *_current;

            iterator_base(const list *lst, node *data)
                : _lst { lst }, _current { data } { }

            public:
            using iterator_category = std::bidirectional_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = VType;
            using pointer = VType *;
            using reference = VType &;

            iterator_base() : _lst { nullptr }, _current { nullptr } { }

            template<typename OVType> requires std::same_as<VType, const OVType>
            iterator_base(const iterator_base<OVType> &other)
                : _lst { other._lst }, _current { other._current } { }

            reference operator*() const { return *_current->get_data(); }
            pointer operator->() const { return _current->get_data(); }
            pointer value() const { return _current->get_data(); }

            iterator_base &operator++()
            {
                if (_current == nullptr)
                    return *this;
                _current = _current->next;
                return *this;
            }

            iterator_base operator++(int)
            {
                iterator_base ret { *this };
                ++(*this);
                return ret;
            }

            iterator_base &operator--()
            {
                if (_current == nullptr)
                    _current = _lst->_tail;
                else
                    _current = _current->prev;
                return *this;
            }

            iterator_base operator--(int)
            {
                iterator_base ret { *this };
                --(*this);
                return ret;
            }

            friend bool operator==(const iterator_base &lhs, const iterator_base &rhs)
            {
                return lhs._lst == rhs._lst && lhs._current == rhs._current;
            }

            friend bool operator!=(const iterator_base &lhs, const iterator_base &rhs)
            {
                return !(lhs == rhs);
            }
        };

        public:
        using value_type = Type;
        using size_type = std::size_t;
        using reference = Type &;
        using const_reference = const Type &;
        using pointer = Type *;
        using const_pointer = const Type *;
        using iterator = iterator_base<Type>;
        using const_iterator = iterator_base<const Type>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        private:
        template<typename ...Args>
        node *allocate(Args &&...args)
        {
            return new node { std::forward<Args>(args)... };
        }

        void deallocate(node *node) { delete node; }

        template<typename ...Args>
        node *insert_before(node *before, Args &&...args)
        {
            auto *new_node = allocate(std::forward<Args>(args)...);
            if (before == nullptr)
            {
                if (_tail == nullptr)
                {
                    bug_on(_head != nullptr);
                    _head = new_node;
                    _tail = new_node;
                }
                else
                {
                    bug_on(_head == nullptr);
                   _tail->next = new_node;
                    new_node->prev = _tail;
                    _tail = new_node;
                }
            }
            else
            {
                new_node->next = before;
                new_node->prev = before->prev;
                before->prev = new_node;
                if (new_node->prev != nullptr)
                    new_node->prev->next = new_node;
                else
                    _head = new_node;
            }
            _size++;
            return new_node;
        }

        void erase_one(node *to_erase)
        {
            if (to_erase == nullptr)
                return;

            if (to_erase->prev == nullptr)
            {
                bug_on(_head != to_erase);
                _head = to_erase->next;
            }
            else to_erase->prev->next = to_erase->next;

            if (to_erase->next == nullptr)
            {
                bug_on(_tail != to_erase);
                _tail = to_erase->prev;
            }
            else to_erase->next->prev = to_erase->prev;

            deallocate(to_erase);
            _size--;
        }

        public:
        list()
            : _head { nullptr }, _tail { nullptr }, _size { 0 } { }

        list(const list &) = delete;
        list(list &&rhs)
            : _head { rhs._head }, _tail { rhs._tail }, _size { rhs._size }
        {
            rhs._head = nullptr;
            rhs._tail = nullptr;
            rhs._size = 0;
        }

        ~list()
        {
            clear();
        }

        list &operator=(const list &) = delete;
        list &operator=(list &&rhs)
        {
            if (this != &rhs)
            {
                clear();

                _head = rhs._head;
                _tail = rhs._tail;
                _size = rhs._size;

                rhs._head = nullptr;
                rhs._tail = nullptr;
                rhs._size = 0;
            }
            return *this;
        }

        iterator insert(iterator pos, const Type &value)
        {
            return iterator { this, insert_before(pos._current, value) };
        }

        iterator insert(const_iterator pos, const Type &value)
        {
            return iterator { this, insert_before(pos._current, value) };
        }

        iterator insert(iterator pos, Type &&value)
        {
            return iterator { this, insert_before(pos._current, std::move(value)) };
        }

        iterator insert(const_iterator pos, Type &&value)
        {
            return iterator { this, insert_before(pos._current, std::move(value)) };
        }

        iterator push_back(const Type &value)
        {
            return insert(end(), value);
        }

        iterator push_back(Type &&value)
        {
            return insert(end(), std::move(value));
        }

        iterator push_front(const Type &value)
        {
            return insert(begin(), value);
        }

        iterator push_front(Type &&value)
        {
            return insert(begin(), std::move(value));
        }

        template<typename ...Args>
        reference emplace_back(Args &&...args)
        {
            return *insert_before(nullptr, std::forward<Args>(args)...)->get_data();
        }

        template<typename ...Args>
        reference emplace_front(Args &&...args)
        {
            return *insert_before(_head, std::forward<Args>(args)...)->get_data();
        }

        void erase(iterator pos)
        {
            erase_one(pos._current);
        }

        void erase(const_iterator pos)
        {
            erase_one(pos._current);
        }

        void pop_front()
        {
            bug_on(_head == nullptr);
            erase_one(_head);
        }

        void pop_back()
        {
            bug_on(_tail == nullptr);
            erase_one(_tail);
        }

        void clear()
        {
            while (_head != nullptr)
            {
                auto *to_erase = _head;
                _head = _head->next;
                deallocate(to_erase);
            }
            _tail = nullptr;
            _size = 0;
        }

        iterator begin() { return { this, _head }; }
        iterator end() { return { this, nullptr }; }

        const_iterator begin() const { return { this, _head }; }
        const_iterator end() const { return { this, nullptr }; }

        const_iterator cbegin() const { return { this, _head }; }
        const_iterator cend() const { return { this, nullptr }; }

        reverse_iterator rbegin() { return reverse_iterator { end() }; }
        reverse_iterator rend() { return reverse_iterator { begin() }; }

        const_reverse_iterator rbegin() const { return const_reverse_iterator { end() }; }
        const_reverse_iterator rend() const { return const_reverse_iterator { begin() }; }

        const_reverse_iterator rcbegin() const { return const_reverse_iterator { end() }; }
        const_reverse_iterator rcend() const { return const_reverse_iterator { begin() }; }

        reference front()
        {
            bug_on(_head == nullptr);
            return *_head->get_data();
        }

        const_reference front() const
        {
            bug_on(_head == nullptr);
            return *_head->get_data();
        }

        reference back()
        {
            bug_on(_tail == nullptr);
            return *_tail->get_data();
        }

        const_reference back() const
        {
            bug_on(_tail == nullptr);
            return *_tail->get_data();
        }

        size_type size() const { return _size; }
        bool empty() const { return _size == 0; }
    };
} // export namespace lib
