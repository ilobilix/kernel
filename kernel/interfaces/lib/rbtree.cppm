// Copyright (C) 2024-2025  ilobilo

export module lib:rbtree;

import :bug_on;
import std;

namespace lib
{
    enum class colour { red, black };
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

        static inline Type *nil() { return nullptr; }
        inline rbtree_hook *hook(rbtree_hook *nh, Type *item)
        {
            if (item == nil())
                return nh;
            return &(item->*Member);
        }

        inline Type *parent(rbtree_hook *nh, Type *item)
        {
            return static_cast<Type *>(hook(nh, item)->parent);
        }

        inline Type *left(rbtree_hook *nh, Type *item)
        {
            return static_cast<Type *>(hook(nh, item)->left);
        }

        inline Type *right(rbtree_hook *nh, Type *item)
        {
            return static_cast<Type *>(hook(nh, item)->right);
        }

        inline Type *successor(rbtree_hook *nh, Type *item)
        {
            return static_cast<Type *>(hook(nh, item)->successor);
        }

        inline Type *predecessor(rbtree_hook *nh, Type *item)
        {
            return static_cast<Type *>(hook(nh, item)->predecessor);
        }

        inline colour colour_of(rbtree_hook *nh, Type *item)
        {
            return hook(nh, item)->colour;
        }

        inline Type *root() const { return static_cast<Type *>(_root); }
        inline Type *head() const { return static_cast<Type *>(_head); }

        void rotate_left(rbtree_hook *nh, Type *x)
        {
            bug_on(right(nh, x) == nil() || parent(nh, root()) != nil());
            auto y = right(nh, x);
            if ((hook(nh, x)->right = left(nh, y)) != nil())
                hook(nh, left(nh, y))->parent = x;
            if ((hook(nh, y)->parent = parent(nh, x)) == nil())
                _root = y;
            else if (x == left(nh, parent(nh, x)))
                hook(nh, parent(nh, x))->left = y;
            else
                hook(nh, parent(nh, x))->right = y;
            hook(nh, y)->left = x;
            hook(nh, x)->parent = y;
        }

        void rotate_right(rbtree_hook *nh, Type *x)
        {
            bug_on(left(nh, x) == nil() || parent(nh, root()) != nil());
            auto y = left(nh, x);
            if ((hook(nh, x)->left = right(nh, y)) != nil())
                hook(nh, right(nh, y))->parent = x;
            if ((hook(nh, y)->parent = parent(nh, x)) == nil())
                _root = y;
            else if (x == right(nh, parent(nh, x)))
                hook(nh, parent(nh, x))->right = y;
            else
                hook(nh, parent(nh, x))->left = y;
            hook(nh, y)->right = x;
            hook(nh, x)->parent = y;
        }

        void insert_fixup(rbtree_hook *nh, Type *z)
        {
            while (colour_of(nh, parent(nh, z)) == colour::red)
            {
                if (parent(nh, z) == left(nh, parent(nh, parent(nh, z))))
                {
                    auto y = right(nh, parent(nh, parent(nh, z)));
                    if (colour_of(nh, y) == colour::red)
                    {
                        hook(nh, parent(nh, z))->colour = colour::black;
                        hook(nh, y)->colour = colour::black;
                        hook(nh, parent(nh, parent(nh, z)))->colour = colour::red;
                        z = parent(nh, parent(nh, z));
                    }
                    else
                    {
                        if (z == right(nh, parent(nh, z)))
                        {
                            z = parent(nh, z);
                            rotate_left(nh, z);
                        }
                        hook(nh, parent(nh, z))->colour = colour::black;
                        hook(nh, parent(nh, parent(nh, z)))->colour = colour::red;
                        rotate_right(nh, parent(nh, parent(nh, z)));
                    }
                }
                else
                {
                    auto y = left(nh, parent(nh, parent(nh, z)));
                    if (colour_of(nh, y) == colour::red)
                    {
                        hook(nh, parent(nh, z))->colour = colour::black;
                        hook(nh, y)->colour = colour::black;
                        hook(nh, parent(nh, parent(nh, z)))->colour = colour::red;
                        z = parent(nh, parent(nh, z));
                    }
                    else
                    {
                        if (z == left(nh, parent(nh, z)))
                        {
                            z = parent(nh, z);
                            rotate_right(nh, z);
                        }
                        hook(nh, parent(nh, z))->colour = colour::black;
                        hook(nh, parent(nh, parent(nh, z)))->colour = colour::red;
                        rotate_left(nh, parent(nh, parent(nh, z)));
                    }
                }
            }
            hook(nh, root())->colour = colour::black;
        }

        void _insert(rbtree_hook *nh, Type *z)
        {
            bug_on(!z || z == nil());

            auto x = root();
            auto y = nil();
            while (x != nil())
            {
                y = x;
                if (_less(*z, *x))
                    x = left(nh, x);
                else
                    x = right(nh, x);
            }
            if ((hook(nh, z)->parent = y) == nil())
            {
                _root = z;
            }
            else if (_less(*z, *y))
            {
                hook(nh, y)->left = z;

                hook(nh, z)->successor = y;
                auto prev = predecessor(nh, y);
                hook(nh, z)->predecessor = prev;
                if (prev != nil())
                    hook(nh, prev)->successor = z;
                hook(nh, y)->predecessor = z;
            }
            else
            {
                hook(nh, y)->right = z;

                hook(nh, z)->predecessor = y;
                auto succ = successor(nh, y);
                hook(nh, z)->successor = succ;
                if (succ != nil())
                    hook(nh, succ)->predecessor = z;
                hook(nh, y)->successor = z;
            }

            hook(nh, z)->left = nil();
            hook(nh, z)->right = nil();
            hook(nh, z)->colour = colour::red;

            if (_head == nil() || _less(*z, *head()))
                _head = z;

            insert_fixup(nh, z);
        }

        void transplant(rbtree_hook *nh, Type *u, Type *v)
        {
            if (parent(nh, u) == nil())
                _root = v;
            else if (u == left(nh, parent(nh, u)))
                hook(nh, parent(nh, u))->left = v;
            else
                hook(nh, parent(nh, u))->right = v;

            hook(nh, v)->parent = parent(nh, u);
        }

        Type *minimum(rbtree_hook *nh, Type *x)
        {
            bug_on(x == nil());
            while (left(nh, x) != nil())
                x = left(nh, x);
            return x;
        }

        Type *maximum(rbtree_hook *nh, Type *x)
        {
            bug_on(x == nil());
            while (right(nh, x) != nil())
                x = right(nh, x);
            return x;
        }

        void _remove_fixup(rbtree_hook *nh, Type *x)
        {
            while (x != root() && colour_of(nh, x) == colour::black)
            {
                if (x == left(nh, parent(nh, x)))
                {
                    auto w = right(nh, parent(nh, x));
                    if (hook(nh, w)->colour == colour::red)
                    {
                        hook(nh, w)->colour = colour::black;
                        hook(nh, parent(nh, x))->colour = colour::red;
                        rotate_left(nh, parent(nh, x));
                        w = right(nh, parent(nh, x));
                    }
                    if (colour_of(nh, left(nh, w)) == colour::black && colour_of(nh, right(nh, w)) == colour::black)
                    {
                        hook(nh, w)->colour = colour::red;
                        x = parent(nh, x);
                    }
                    else
                    {
                        if (colour_of(nh, right(nh, w)) == colour::black)
                        {
                            hook(nh, left(nh, w))->colour = colour::black;
                            hook(nh, w)->colour = colour::red;
                            rotate_right(nh, w);
                            w = right(nh, parent(nh, x));
                        }
                        hook(nh, w)->colour = hook(nh, parent(nh, x))->colour;
                        hook(nh, parent(nh, x))->colour = colour::black;
                        hook(nh, right(nh, w))->colour = colour::black;
                        rotate_left(nh, parent(nh, x));
                        x = root();
                    }
                }
                else
                {
                    auto w = left(nh, parent(nh, x));
                    if (colour_of(nh, w) == colour::red)
                    {
                        hook(nh, w)->colour = colour::black;
                        hook(nh, parent(nh, x))->colour = colour::red;
                        rotate_right(nh, parent(nh, x));
                        w = left(nh, parent(nh, x));
                    }
                    if (colour_of(nh, right(nh, w)) == colour::black && colour_of(nh, left(nh, w)) == colour::black)
                    {
                        hook(nh, w)->colour = colour::red;
                        x = parent(nh, x);
                    }
                    else
                    {
                        if (colour_of(nh, left(nh, w)) == colour::black)
                        {
                            hook(nh, right(nh, w))->colour = colour::black;
                            hook(nh, w)->colour = colour::red;
                            rotate_left(nh, w);
                            w = left(nh, parent(nh, x));
                        }
                        hook(nh, w)->colour = hook(nh, parent(nh, x))->colour;
                        hook(nh, parent(nh, x))->colour = colour::black;
                        hook(nh, left(nh, w))->colour = colour::black;
                        rotate_right(nh, parent(nh, x));
                        x = root();
                    }
                }
            }
            hook(nh, x)->colour = colour::black;
        }

        void _remove(rbtree_hook *nh, Type *z)
        {
            bug_on(!z || z == nil());

            auto pred = predecessor(nh, z);
            auto succ = successor(nh, z);

            bug_on(pred != nil() && successor(nh, pred) != z);
            bug_on(succ != nil() && predecessor(nh, succ) != z);

            if (pred != nil())
                hook(nh, pred)->successor = succ;
            else
                _head = (succ != nil()) ? succ : nil();

            if (succ != nil())
                hook(nh, succ)->predecessor = pred;

            if (_head == z)
                _head = (succ != nil()) ? succ : (pred != nil() ? pred : nil());

            hook(nh, z)->predecessor = nil();
            hook(nh, z)->successor = nil();

            auto x = nil();
            auto y = z;
            auto yoc = colour_of(nh, y);

            if (left(nh, z) == nil())
            {
                x = right(nh, z);
                transplant(nh, z, right(nh, z));
            }
            else if (right(nh, z) == nil())
            {
                x = left(nh, z);
                transplant(nh, z, left(nh, z));
            }
            else
            {
                y = minimum(nh, right(nh, z));
                yoc = colour_of(nh, y);
                x = right(nh, y);

                if (y != right(nh, z))
                {
                    transplant(nh, y, right(nh, y));
                    hook(nh, y)->right = right(nh, z);
                    hook(nh, right(nh, y))->parent = y;
                }
                else hook(nh, x)->parent = y;

                transplant(nh, z, y);
                hook(nh, y)->left = left(nh, z);
                hook(nh, left(nh, y))->parent = y;
                hook(nh, y)->colour = hook(nh, z)->colour;
            }
            if (yoc == colour::black)
                _remove_fixup(nh, x);

            if (root() == nil())
                _head = nil();
            else
                bug_on(_head == nil() || predecessor(nh, head()) != nil());
        }

        class iterator
        {
            template<typename Type1, rbtree_hook Type1::*, typename>
            friend class rbtree;

            private:
            rbtree *_tree;
            Type *_current;
            iterator(rbtree *tree, Type *data) : _tree { tree }, _current { data } { }

            public:
            Type &operator*() const { return *_current; }
            Type *operator->() const { return _current; }
            Type *value() const { return _current; }

            iterator &operator++()
            {
                rbtree_hook nh;
                _current = _tree->successor(&nh, _current);
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
                rbtree_hook nh;
                _current = _tree->predecessor(&nh, _current);
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
                return lhs._tree == rhs._tree && lhs._current == rhs._current;
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
            bug_on(!z);
            rbtree_hook nh;
            *hook(&nh, z) = rbtree_hook { };

            if (_root == nil())
            {
                _root = z;
                _head = _root;
            }
            else _insert(&nh, z);

            _size++;
        }

        void remove(Type *x)
        {
            bug_on(!x);
            rbtree_hook nh;
            _remove(&nh, x);
            bug_on(_size == 0);
            _size--;
        }

        void remove(iterator x)
        {
            bug_on(x._tree != this);
            remove(x.value());
        }

        iterator begin() { return { this, head() }; }
        iterator end() { return { this, nil() }; }

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
            rbtree_hook nh;
            auto ret = maximum(&nh, root());
            return ret == nil() ? nullptr : ret;
        }

        bool contains(Type *x) const
        {
            auto current = root();
            auto equal = [&](Type *a, Type *b) {
                return !_less(*a, *b) && !_less(*b, *a);
            };
            rbtree_hook nh;
            while (current != nil() && !equal(current, x))
            {
                if (_less(*x, *current))
                    current = left(&nh, current);
                else
                    current = right(&nh, current);
            }
            return current != nil();
        }

        std::size_t size() const { return _size; }
        bool empty() const { return _size == 0; }
    };
} // export namespace lib