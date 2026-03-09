// Copyright (C) 2024-2026  ilobilo

export module lib:rbtree;

import :bug_on;
import std;

namespace lib
{
    enum class colour { red, black };

    template<typename Type>
    struct default_augmentor
    {
        static constexpr bool operator()(Type *) { return false; }
    };
} // namespace lib

export namespace lib
{
    template<typename IType>
    struct interval_hook;

    template<typename Type>
    struct rbtree_hook
    {
        template<typename Type1, rbtree_hook<Type1> Type1::*, typename, typename>
        friend class rbtree;

        template<
            typename Type1, typename IType,
            IType Type1::*, IType Type1::*,
            rbtree_hook<Type1> Type1::*, interval_hook<IType> Type1::*
        >
        friend class interval_tree;

        template<
            typename Type1, typename IType1,
            IType1 Type1::*, IType1 Type1::*,
            rbtree_hook<Type1> Type1::*, interval_hook<IType1> Type1::*
        >
        friend class interval_tree_alloc;

        private:
        Type *parent;
        Type *left;
        Type *right;
        Type *successor;
        Type *predecessor;
        colour colour;

        public:
        constexpr rbtree_hook() : parent { nullptr },
            left { nullptr }, right { nullptr },
            successor { nullptr }, predecessor { nullptr },
            colour { colour::black } { }
    };

    template<typename Type>
    struct interval_hook;

    template<typename Type, rbtree_hook<Type> Type::*Member, typename Less, typename Aug = default_augmentor<Type>>
    class rbtree
    {

        template<typename Type1, rbtree_hook<Type1> Type1::*, typename, typename>
        friend class rbtree_alloc;

        template<
            typename Type1, typename IType,
            IType Type1::*, IType Type1::*,
            rbtree_hook<Type1> Type1::*, interval_hook<IType> Type1::*
        >
        friend class interval_tree;

        template<
            typename Type1, typename IType1,
            IType1 Type1::*, IType1 Type1::*,
            rbtree_hook<Type1> Type1::*, interval_hook<IType1> Type1::*
        >
        friend class interval_tree_alloc;

        private:
        Type *_root;
        Type *_head;
        std::size_t _size;

        static inline constexpr Type *nil() { return nullptr; }
        inline constexpr rbtree_hook<Type> *hook(rbtree_hook<Type> *nh, Type *item) const
        {
            if (item == nil())
                return nh;
            return &(item->*Member);
        }

        inline constexpr Type *parent(rbtree_hook<Type> *nh, Type *item) const
        {
            return hook(nh, item)->parent;
        }

        inline constexpr Type *left(rbtree_hook<Type> *nh, Type *item) const
        {
            return hook(nh, item)->left;
        }

        inline constexpr Type *right(rbtree_hook<Type> *nh, Type *item) const
        {
            return hook(nh, item)->right;
        }

        inline constexpr Type *successor(rbtree_hook<Type> *nh, Type *item) const
        {
            return hook(nh, item)->successor;
        }

        inline constexpr Type *predecessor(rbtree_hook<Type> *nh, Type *item) const
        {
            return hook(nh, item)->predecessor;
        }

        inline constexpr colour colour_of(rbtree_hook<Type> *nh, Type *item) const
        {
            return hook(nh, item)->colour;
        }

        inline constexpr Type *root() const { return static_cast<Type *>(_root); }
        inline constexpr Type *head() const { return static_cast<Type *>(_head); }

        static inline constexpr bool augment(Type *x)
        {
            if (x != nil())
                return Aug::operator()(x);
            return false;
        }

        constexpr void rotate_left(rbtree_hook<Type> *nh, Type *x)
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

            augment(x);
            augment(y);
        }

        constexpr void rotate_right(rbtree_hook<Type> *nh, Type *x)
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

            augment(x);
            augment(y);
        }

        constexpr void insert_fixup(rbtree_hook<Type> *nh, Type *z)
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

        constexpr void _insert(rbtree_hook<Type> *nh, Type *z)
        {
            bug_on(!z || z == nil());

            auto x = root();
            auto y = nil();
            while (x != nil())
            {
                y = x;
                if (Less::operator()(*z, *x))
                    x = left(nh, x);
                else
                    x = right(nh, x);
            }
            if ((hook(nh, z)->parent = y) == nil())
            {
                _root = z;
            }
            else if (Less::operator()(*z, *y))
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

            if (_head == nil() || Less::operator()(*z, *head()))
                _head = z;

            augment(z);
            auto c = parent(nh, z);
            while (c != nil())
            {
                if (!augment(c))
                    break;
                c = parent(nh, c);
            }

            insert_fixup(nh, z);
        }

        constexpr void transplant(rbtree_hook<Type> *nh, Type *u, Type *v)
        {
            if (parent(nh, u) == nil())
                _root = v;
            else if (u == left(nh, parent(nh, u)))
                hook(nh, parent(nh, u))->left = v;
            else
                hook(nh, parent(nh, u))->right = v;

            hook(nh, v)->parent = parent(nh, u);
        }

        constexpr Type *minimum(rbtree_hook<Type> *nh, Type *x) const
        {
            bug_on(x == nil());
            while (left(nh, x) != nil())
                x = left(nh, x);
            return x;
        }

        constexpr Type *maximum(rbtree_hook<Type> *nh, Type *x) const
        {
            bug_on(x == nil());
            while (right(nh, x) != nil())
                x = right(nh, x);
            return x;
        }

        constexpr void _remove_fixup(rbtree_hook<Type> *nh, Type *x)
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

        constexpr void _remove(rbtree_hook<Type> *nh, Type *z)
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

            // if (_head == z)
            //     _head = (succ != nil()) ? succ : (pred != nil() ? pred : nil());

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

            auto us = parent(nh, x);
            if (yoc == colour::black)
                _remove_fixup(nh, x);

            auto c = us;
            while (c != nil())
            {
                if (!augment(c))
                    break;
                c = parent(nh, c);
            }

            if (root() == nil())
                _head = nil();
            else
                bug_on(_head == nil() || predecessor(nh, head()) != nil());
        }

        template<typename VType>
        class iterator_base
        {
            template<typename Type1, rbtree_hook<Type1> Type1::*, typename, typename>
            friend class rbtree;

            template<typename Type1, rbtree_hook<Type1> Type1::*, typename, typename>
            friend class rbtree_alloc;

            template<
                typename Type1, typename IType1,
                IType1 Type1::*, IType1 Type1::*,
                rbtree_hook<Type1> Type1::*, interval_hook<IType1> Type1::*
            >
            friend class interval_tree_alloc;

            private:
            rbtree *_tree;
            Type *_current;

            constexpr iterator_base(rbtree *tree, Type *data) : _tree { tree }, _current { data } { }
            constexpr iterator_base(const rbtree *tree, Type *data)
                : iterator_base { const_cast<rbtree *>(tree), data } { }

            public:
            using iterator_category = std::bidirectional_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = VType;
            using pointer = VType *;
            using reference = VType &;

            constexpr iterator_base() : _tree { nullptr }, _current { nullptr } { }

            constexpr reference operator*() const { return *_current; }
            constexpr pointer operator->() const { return _current; }
            constexpr pointer value() const { return _current; }

            constexpr iterator_base &operator++()
            {
                rbtree_hook<Type> nh;
                _current = _tree->successor(&nh, _current);
                return *this;
            }

            constexpr iterator_base operator++(int)
            {
                auto ret { *this };
                ++(*this);
                return ret;
            }

            constexpr iterator_base &operator--()
            {
                if (_current == nullptr)
                {
                    _current = _tree->last();
                    return *this;
                }
                rbtree_hook<Type> nh;
                _current = _tree->predecessor(&nh, _current);
                return *this;
            }

            constexpr iterator_base operator--(int)
            {
                auto ret { *this };
                --(*this);
                return ret;
            }

            friend constexpr bool operator==(const iterator_base &lhs, const iterator_base &rhs)
            {
                return (lhs._tree == rhs._tree && lhs._current == rhs._current) ||
                       (lhs._current == nullptr && rhs._current == nullptr);
            }

            friend constexpr bool operator!=(const iterator_base &lhs, const iterator_base &rhs)
            {
                return !(lhs == rhs);
            }
        };

        public:
        using iterator = iterator_base<Type>;
        using const_iterator = iterator_base<const Type>;

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        constexpr rbtree() : _root { nil() }, _head { nil() }, _size { 0 } { }

        constexpr rbtree(const rbtree &) = delete;
        constexpr rbtree(rbtree &&rhs)
            : _root { rhs._root }, _head { rhs._head }, _size { rhs._size }
        {
            rhs._root = nil();
            rhs._head = nil();
            rhs._size = 0;
        }

        constexpr rbtree &operator=(const rbtree &) = delete;
        constexpr rbtree &operator=(rbtree &&rhs)
        {
            if (this != &rhs)
            {
                _root = rhs._root;
                _head = rhs._head;
                _size = rhs._size;

                rhs._root = nil();
                rhs._head = nil();
                rhs._size = 0;
            }
            return *this;
        }

        constexpr void insert(Type *z)
        {
            bug_on(!z);
            rbtree_hook<Type> nh;
            *hook(&nh, z) = rbtree_hook<Type> { };

            if (_root == nil())
            {
                _root = z;
                _head = _root;
            }
            else _insert(&nh, z);

            _size++;
        }

        constexpr void remove(Type *x)
        {
            bug_on(!x);
            rbtree_hook<Type> nh;
            _remove(&nh, x);
            bug_on(_size == 0);
            _size--;
        }

        constexpr void remove(iterator x)
        {
            bug_on(x._tree != this);
            remove(x.value());
        }

        constexpr iterator begin() { return { this, head() }; }
        constexpr iterator end() { return { this, nil() }; }

        constexpr const_iterator begin() const { return { this, head() }; }
        constexpr const_iterator end() const { return { this, nil() }; }

        constexpr const_iterator cbegin() const { return { this, head() }; }
        constexpr const_iterator cend() const { return { this, nil() }; }

        constexpr reverse_iterator rbegin() { return reverse_iterator { end() }; }
        constexpr reverse_iterator rend() { return reverse_iterator { begin() }; }

        constexpr const_reverse_iterator rbegin() const { return const_reverse_iterator { end() }; }
        constexpr const_reverse_iterator rend() const { return const_reverse_iterator { begin() }; }

        constexpr const_reverse_iterator rcbegin() const { return const_reverse_iterator { end() }; }
        constexpr const_reverse_iterator rcend() const { return const_reverse_iterator { begin() }; }

        constexpr Type *first() const
        {
            if (head() == nil())
                return nullptr;
            return head();
        }

        constexpr Type *last() const
        {
            if (root() == nil())
                return nullptr;
            rbtree_hook<Type> nh;
            auto ret = maximum(&nh, root());
            return ret == nil() ? nullptr : ret;
        }

        constexpr bool contains(Type *x) const
        {
            auto current = root();
            auto equal = [&](Type *a, Type *b) {
                return !Less::operator()(*a, *b) && !Less::operator()(*b, *a);
            };
            rbtree_hook<Type> nh;
            while (current != nil() && !equal(current, x))
            {
                if (Less::operator()(*x, *current))
                    current = left(&nh, current);
                else
                    current = right(&nh, current);
            }
            return current != nil();
        }

        constexpr std::size_t size() const { return _size; }
        constexpr bool empty() const { return _size == 0; }
    };

    template<typename Type, rbtree_hook<Type> Type::*Member, typename Less, typename Aug = default_augmentor<Type>>
    class rbtree_alloc
    {
        private:
        rbtree<Type, Member, Less, Aug> _rbtree;

        constexpr void _delete_subtree(Type *node)
        {
            if (!node)
                return;

            auto h = &(node->*Member);
            _delete_subtree(h->left);
            _delete_subtree(h->right);
            delete node;
        }

        public:
        using iterator = typename rbtree<Type, Member, Less, Aug>::iterator;
        using const_iterator = typename rbtree<Type, Member, Less, Aug>::const_iterator;

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        constexpr rbtree_alloc() : _rbtree { } { }

        constexpr rbtree_alloc(const rbtree_alloc &) = delete;
        constexpr rbtree_alloc(rbtree_alloc &&rhs) : _rbtree { std::move(rhs._rbtree) }
        { rhs._rbtree = { }; }

        constexpr ~rbtree_alloc() { clear(); }

        constexpr rbtree_alloc &operator=(const rbtree_alloc &) = delete;
        constexpr rbtree_alloc &operator=(rbtree_alloc &&rhs)
        {
            if (this != &rhs)
            {
                clear();
                _rbtree = std::move(rhs._rbtree);
                rhs._rbtree = { };
            }
            return *this;
        }

        template<typename... Args>
        constexpr iterator emplace(Args&&... args)
        {
            auto obj = new Type(std::forward<Args>(args)...);
            _rbtree.insert(obj);
            return iterator { &_rbtree, obj };
        }

        constexpr void remove(Type *x)
        {
            _rbtree.remove(x);
            delete x;
        }

        constexpr iterator remove(iterator x)
        {
            bug_on(x._tree != &_rbtree);
            auto next = x;
            ++next;
            remove(x.value());
            return next;
        }

        constexpr const_iterator remove(const_iterator x)
        {
            bug_on(x._tree != &_rbtree);
            auto next = x;
            ++next;
            remove(const_cast<Type *>(x.value()));
            return next;
        }

        constexpr void clear()
        {
            if (_rbtree.empty())
                return;
            _delete_subtree(_rbtree.root());
            _rbtree = { };
        }

        constexpr iterator begin() { return _rbtree.begin(); }
        constexpr iterator end() { return _rbtree.end(); }

        constexpr const_iterator begin() const { return _rbtree.begin(); }
        constexpr const_iterator end() const { return _rbtree.end(); }

        constexpr const_iterator cbegin() const { return _rbtree.cbegin(); }
        constexpr const_iterator cend() const { return _rbtree.cend(); }

        constexpr reverse_iterator rbegin() { return reverse_iterator { end() }; }
        constexpr reverse_iterator rend() { return reverse_iterator { begin() }; }

        constexpr const_reverse_iterator rbegin() const { return const_reverse_iterator { end() }; }
        constexpr const_reverse_iterator rend() const { return const_reverse_iterator { begin() }; }

        constexpr const_reverse_iterator rcbegin() const { return const_reverse_iterator { end() }; }
        constexpr const_reverse_iterator rcend() const { return const_reverse_iterator { begin() }; }

        constexpr std::size_t size() const { return _rbtree.size(); }
        constexpr bool empty() const { return _rbtree.empty(); }
    };
} // export namespace lib
