// Copyright (C) 2024-2025  ilobilo

export module lib:rbtree;

import :bug_on;
import cppstd;

namespace lib
{
    enum class colour
    {
        red, black
    };
} // namespace lib

export namespace lib
{
    struct rbtree_hook
    {
        template<typename Type, rbtree_hook Type::*, typename>
        friend class rbtree;

        private:
        void *parent;
        void *left;
        void *right;
        void *successor;
        void *predecessor;
        colour colour;

        public:
        constexpr rbtree_hook() : parent { nullptr },
            left { nullptr }, right { nullptr },
            successor { nullptr }, predecessor { nullptr },
            colour { colour::black } { }
    };

    template<typename Type, rbtree_hook Type::*Member, typename Less>
    class rbtree
    {
        private:
        void *_root;
        void *_head;
        std::size_t _size;
        Less _less;

        static rbtree_hook *hook(Type *item)
        {
            return &(item->*Member);
        }

        static Type *parent(Type *item)
        {
            return static_cast<Type *>(hook(item)->parent);
        }

        static Type *left(Type *item)
        {
            return static_cast<Type *>(hook(item)->left);
        }

        static Type *right(Type *item)
        {
            return static_cast<Type *>(hook(item)->right);
        }

        static Type *successor(Type *item)
        {
            return static_cast<Type *>(hook(item)->successor);
        }

        static Type *predecessor(Type *item)
        {
            return static_cast<Type *>(hook(item)->predecessor);
        }

        static Type *nil()
        {
            alignas(alignof(Type)) static std::byte buffer[sizeof(Type)];
            static auto *ptr = std::launder(reinterpret_cast<Type *>(buffer));
            [[maybe_unused]]
            static bool once = [&] {
                auto h = hook(ptr);
                std::construct_at(h);
                h->parent = ptr;
                h->left = ptr;
                h->right = ptr;
                h->successor = ptr;
                h->predecessor = ptr;
                h->colour = colour::black;
                return true;
            } ();
            bug_on(hook(ptr)->colour != colour::black);
            return ptr;
        }

        Type *root() const { return static_cast<Type *>(_root); }
        Type *head() const { return static_cast<Type *>(_head); }

        void rotate_left(Type *x)
        {
            bug_on(right(x) == nil() || parent(root()) != nil());
            auto y = right(x);
            if ((hook(x)->right = left(y)) != nil())
                hook(left(y))->parent = x;
            if ((hook(y)->parent = parent(x)) == nil())
                _root = y;
            else if (x == left(parent(x)))
                hook(parent(x))->left = y;
            else
                hook(parent(x))->right = y;
            hook(y)->left = x;
            hook(x)->parent = y;
        }

        void rotate_right(Type *y)
        {
            bug_on(left(y) == nil() || parent(root()) != nil());
            auto x = left(y);
            bug_on(x == nullptr);
            if ((hook(y)->left = right(x)) != nil())
                hook(right(x))->parent = y;
            if ((hook(x)->parent = parent(y)) == nil())
                _root = x;
            else if (y == left(parent(y)))
                hook(parent(y))->left = x;
            else
                hook(parent(y))->right = x;
            hook(x)->right = y;
            hook(y)->parent = x;
        }

        void insert_fixup(Type *z)
        {
            while (hook(parent(z))->colour == colour::red)
            {
                if (parent(z) == left(parent(parent(z))))
                {
                    auto y = right((parent(parent(z))));
                    if (hook(y)->colour == colour::red)
                    {
                        hook(parent(z))->colour = colour::black;
                        hook(y)->colour = colour::black;
                        hook(parent(parent(z)))->colour = colour::red;
                        z = parent(parent(z));
                    }
                    else
                    {
                        if (z == right(parent(z)))
                        {
                            z = parent(z);
                            rotate_left(z);
                        }
                        hook(parent(z))->colour = colour::black;
                        hook(parent(parent(z)))->colour = colour::red;
                        rotate_right(parent(parent(z)));
                    }
                }
                else
                {
                    auto y = left((parent(parent(z))));
                    if (hook(y)->colour == colour::red)
                    {
                        hook(parent(z))->colour = colour::black;
                        hook(y)->colour = colour::black;
                        hook(parent(parent(z)))->colour = colour::red;
                        z = parent(parent(z));
                    }
                    else
                    {
                        if (z == left(parent(z)))
                        {
                            z = parent(z);
                            rotate_right(z);
                        }
                        hook(parent(z))->colour = colour::black;
                        hook(parent(parent(z)))->colour = colour::red;
                        rotate_left(parent(parent(z)));
                    }
                }
            }
            hook(root())->colour = colour::black;
        }

        void _insert(Type *z)
        {
            bug_on(!z || z == nil());

            auto x = root();
            auto y = nil();
            while (x != nil())
            {
                y = x;
                if (_less(*z, *x))
                    x = left(x);
                else
                    x = right(x);
            }
            if ((hook(z)->parent = y) == nil())
            {
                _root = z;
            }
            else if (_less(*z, *y))
            {
                hook(y)->left = z;

                hook(z)->successor = y;
                auto prev = predecessor(y);
                hook(z)->predecessor = prev;
                if (prev != nil())
                    hook(prev)->successor = z;
                hook(y)->predecessor = z;
            }
            else
            {
                hook(y)->right = z;

                hook(z)->predecessor = y;
                auto succ = successor(y);
                hook(z)->successor = succ;
                if (succ != nil())
                    hook(succ)->predecessor = z;
                hook(y)->successor = z;
            }

            hook(z)->left = nil();
            hook(z)->right = nil();
            hook(z)->colour = colour::red;

            if (_head == nil() || _less(*z, *head()))
                _head = z;

            insert_fixup(z);
        }

        void transplant(Type *u, Type *v)
        {
            if (parent(u) == nil())
                _root = v;
            else if (u == left(parent(u)))
                hook(parent(u))->left = v;
            else
                hook(parent(u))->right = v;

            hook(v)->parent = parent(u);
        }

        Type *minimum(Type *x)
        {
            bug_on(x == nil());
            while (left(x) != nil())
                x = left(x);
            return x;
        }

        Type *maximum(Type *x)
        {
            bug_on(x == nil());
            while (right(x) != nil())
                x = right(x);
            return x;
        }

        void _remove_fixup(Type *x)
        {
            while (x != root() && hook(x)->colour == colour::black)
            {
                if (x == left(parent(x)))
                {
                    auto w = right(parent(x));
                    if (hook(w)->colour == colour::red)
                    {
                        hook(w)->colour = colour::black;
                        hook(parent(x))->colour = colour::red;
                        rotate_left(parent(x));
                        w = right(parent(x));
                    }
                    if (hook(left(w))->colour == colour::black && hook(right(w))->colour == colour::black)
                    {
                        hook(w)->colour = colour::red;
                        x = parent(x);
                    }
                    else
                    {
                        if (hook(right(w))->colour == colour::black)
                        {
                            hook(left(w))->colour = colour::black;
                            hook(w)->colour = colour::red;
                            rotate_right(w);
                            w = right(parent(x));
                        }
                        hook(w)->colour = hook(parent(x))->colour;
                        hook(parent(x))->colour = colour::black;
                        hook(right(w))->colour = colour::black;
                        rotate_left(parent(x));
                        x = root();
                    }
                }
                else
                {
                    auto w = left(parent(x));
                    if (hook(w)->colour == colour::red)
                    {
                        hook(w)->colour = colour::black;
                        hook(parent(x))->colour = colour::red;
                        rotate_right(parent(x));
                        w = left(parent(x));
                    }
                    if (hook(right(w))->colour == colour::black && hook(left(w))->colour == colour::black)
                    {
                        hook(w)->colour = colour::red;
                        x = parent(x);
                    }
                    else
                    {
                        if (hook(left(w))->colour == colour::black)
                        {
                            hook(right(w))->colour = colour::black;
                            hook(w)->colour = colour::red;
                            rotate_left(w);
                            w = left(parent(x));
                        }
                        hook(w)->colour = hook(parent(x))->colour;
                        hook(parent(x))->colour = colour::black;
                        hook(left(w))->colour = colour::black;
                        rotate_right(parent(x));
                        x = root();
                    }
                }
            }
            hook(x)->colour = colour::black;
        }

        void _remove(Type *z)
        {
            bug_on(!z || z == nil());

            auto pred = predecessor(z);
            auto succ = successor(z);

            bug_on(pred != nil() && successor(pred) != z);
            bug_on(succ != nil() && predecessor(succ) != z);

            if (pred != nil())
                hook(pred)->successor = succ;
            else
                _head = (succ != nil()) ? succ : nil();

            if (succ != nil())
                hook(succ)->predecessor = pred;

            if (_head == z)
                _head = (succ != nil()) ? succ : (pred != nil() ? pred : nil());

            hook(z)->predecessor = nil();
            hook(z)->successor = nil();

            auto x = nil();
            auto y = z;
            auto yoc = hook(y)->colour;

            if (left(z) == nil())
            {
                x = right(z);
                transplant(z, right(z));
            }
            else if (right(z) == nil())
            {
                x = left(z);
                transplant(z, left(z));
            }
            else
            {
                y = minimum(right(z));
                yoc = hook(y)->colour;
                x = right(y);

                if (y != right(z))
                {
                    transplant(y, right(y));
                    hook(y)->right = right(z);
                    hook(right(y))->parent = y;
                }
                else hook(x)->parent = y;

                transplant(z, y);
                hook(y)->left = left(z);
                hook(left(y))->parent = y;
                hook(y)->colour = hook(z)->colour;
            }
            if (yoc == colour::black)
                _remove_fixup(x);

            if (root() == nil())
                _head = nil();
            else
                bug_on(_head == nil() || predecessor(head()) != nil());
        }

        class iterator
        {
            template<typename Type1, rbtree_hook Type1::*, typename>
            friend class rbtree;

            private:
            Type *_current;
            iterator(Type *data) : _current { data } { }

            public:
            Type &operator*() const { return *_current; }
            Type *operator->() const { return _current; }
            Type *value() const { return _current; }

            iterator &operator++()
            {
                _current = successor(_current);
                return *this;
            }

            iterator operator++(int)
            {
                auto ret { *this };
                ++(*this);
                return ret;
            }

            iterator &operator--()
            {
                _current = predecessor(_current);
                return *this;
            }

            iterator operator--(int)
            {
                auto ret { *this };
                --(*this);
                return ret;
            }

            friend bool operator==(const iterator &lhs, const iterator &rhs)
            {
                return lhs._current == rhs._current;
            }

            friend bool operator!=(const iterator &lhs, const iterator &rhs)
            {
                return !(lhs == rhs);
            }
        };

        public:
        rbtree() : _root { nil() }, _head { nil() }, _size { 0 }, _less { } { }

        rbtree(const rbtree &) = delete;
        rbtree(rbtree &&rhs)
            : _root { rhs._root }, _head { rhs._head },
              _size { rhs._size }, _less { std::move(rhs._less) }
        {
            rhs._root = nil();
            rhs._head = nil();
            rhs._size = 0;
        }

        rbtree &operator=(const rbtree &) = delete;
        rbtree &operator=(rbtree &&rhs)
        {
            if (this != &rhs)
            {
                _root = rhs._root;
                _head = rhs._head;
                _size = rhs._size;
                _less = std::move(rhs._less);

                rhs._root = nil();
                rhs._head = nil();
                rhs._size = 0;
            }
            return *this;
        }

        void insert(Type *z)
        {
            auto h = hook(z);
            h->parent = h->left = h->right = nil();
            h->predecessor = h->successor = nil();

            if (_root == nil())
            {
                _root = z;
                _head = _root;
                hook(root())->parent = nil();
                hook(root())->colour = colour::black;
            }
            else _insert(z);

            _size++;
        }

        void remove(Type *x)
        {
            bug_on(!x || x == nil());
            auto h = hook(x);
            bug_on(
                !h->parent || !h->left || !h->right ||
                !h->predecessor || !h->successor
            );
            _remove(x);

            h->parent = h->left = h->right = nil();
            h->predecessor = h->successor = nil();

            bug_on(_size == 0);
            _size--;
        }

        void remove(iterator x) { remove(x.value()); }

        iterator begin() { return head(); }
        iterator end() { return nil(); }

        Type *first()
        {
            if (head() == nil())
                return nullptr;
            return head();
        }

        Type *last()
        {
            if (root() == nil())
                return nullptr;
            auto ret = maximum(root());
            return ret == nil() ? nullptr : ret;
        }

        bool contains(Type *x) const
        {
            auto current = root();
            auto equal = [&](Type *a, Type *b) {
                return !_less(*a, *b) && !_less(*b, *a);
            };
            while (current != nil() && !equal(current, x))
            {
                if (_less(*x, *current))
                    current = left(current);
                else
                    current = right(current);
            }
            return current != nil();
        }

        std::size_t size() const { return _size; }
        bool empty() const { return _size == 0; }
    };
} // export namespace lib