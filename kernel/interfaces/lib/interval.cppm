// Copyright (C) 2024-2026  ilobilo

export module lib:interval;

import :rbtree;
import :bug_on;
import std;

export namespace lib
{
    template<typename IType>
    struct interval_hook
    {
        IType subtree_max;
    };

    template<
        typename Type, typename IType,
        IType Type::*Lower, IType Type::*Upper,
        rbtree_hook<Type> Type::*Hook, interval_hook<IType> Type::*IHook
    >
    class interval_tree
    {
        template<
            typename Type1, typename IType1,
            IType1 Type1::*, IType1 Type1::*,
            rbtree_hook<Type1> Type1::*, interval_hook<IType1> Type1::*
        >
        friend class interval_tree_alloc;

        private:
        static constexpr IType lower(const Type *node)
        {
            return node->*Lower;
        }

        static constexpr IType upper(const Type *node)
        {
            return node->*Upper;
        }

        static constexpr rbtree_hook<Type> *rhook(Type *node)
        {
            return &(node->*Hook);
        }

        static constexpr interval_hook<IType> *ihook(Type *node)
        {
            return &(node->*IHook);
        }

        struct less
        {
            static constexpr bool operator()(const Type &a, const Type &b)
            {
                return lower(&a) < lower(&b);
            }
        };

        struct augmentor
        {
            static constexpr bool operator()(Type *node)
            {
                auto new_max = upper(node);
                auto left = rhook(node)->left;
                auto right = rhook(node)->right;

                if (left != nullptr)
                {
                    const auto left_max = ihook(left)->subtree_max;
                    if (left_max > new_max)
                        new_max = left_max;
                }

                if (right != nullptr)
                {
                    const auto right_max = ihook(right)->subtree_max;
                    if (right_max > new_max)
                        new_max = right_max;
                }

                if (new_max == ihook(node)->subtree_max)
                    return false;

                ihook(node)->subtree_max = new_max;
                return true;
            }
        };

        rbtree<Type, Hook, less, augmentor> _rbtree;

        template<typename VType>
        class overlapping_iterator_base
        {
            template<
                typename Type1, typename IType1,
                IType1 Type1::*, IType1 Type1::*,
                rbtree_hook<Type1> Type1::*, interval_hook<IType1> Type1::*
            >
            friend class interval_tree;

            template<
                typename Type1, typename IType1,
                IType1 Type1::*, IType1 Type1::*,
                rbtree_hook<Type1> Type1::*, interval_hook<IType1> Type1::*
            >
            friend class interval_tree_alloc;

            private:
            interval_tree *_tree;
            Type *_current;
            IType _lb;
            IType _ub;

            enum class state { left, visit_right, up };

            constexpr bool overlaps(Type *node) const
            {
                return interval_tree::lower(node) < _ub && _lb < interval_tree::upper(node);
            }

            constexpr void search(Type *node, state s)
            {
                while (node)
                {
                    switch (s)
                    {
                        case state::left:
                        {
                            const auto left = interval_tree::rhook(node)->left;
                            if (left && interval_tree::ihook(left)->subtree_max > _lb)
                            {
                                node = left;
                                break;
                            }
                            s = state::visit_right;
                            [[fallthrough]];
                        }
                        case state::visit_right:
                        {
                            if (overlaps(node))
                            {
                                _current = node;
                                return;
                            }

                            const auto right = interval_tree::rhook(node)->right;
                            if (right && interval_tree::lower(node) < _ub)
                            {
                                node = right;
                                s = state::left;
                                break;
                            }
                            s = state::up;
                            [[fallthrough]];
                        }
                        case state::up:
                        {
                            const auto parent = interval_tree::rhook(node)->parent;
                            if (!parent)
                            {
                                _current = nullptr;
                                return;
                            }
                            if (node == interval_tree::rhook(parent)->left)
                            {
                                node = parent;
                                s = state::visit_right;
                            }
                            else
                            {
                                node = parent;
                                s = state::up;
                            }
                            break;
                        }
                    }
                }
                _current = nullptr;
            }

            constexpr void advance()
            {
                if (!_current)
                    return;

                auto node = _current;
                const auto right = interval_tree::rhook(node)->right;
                if (right && interval_tree::lower(node) < _ub)
                    search(right, state::left);
                else
                    search(node, state::up);
            }

            constexpr overlapping_iterator_base(interval_tree *tree, Type *root, IType lb, IType ub)
                : _tree { tree }, _current { nullptr }, _lb { lb }, _ub { ub }
            {
                if (root)
                    search(root, state::left);
            }


            constexpr overlapping_iterator_base(const interval_tree *tree, Type *root, IType lb, IType ub)
                : overlapping_iterator_base { const_cast<interval_tree *>(tree), root, lb, ub } { }

            public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = VType;
            using pointer = VType *;
            using reference = VType &;

            constexpr overlapping_iterator_base()
                : _tree { nullptr }, _current { nullptr }, _lb { 0 }, _ub { 0 } { }

            constexpr reference operator*() const { return *_current; }
            constexpr pointer operator->() const { return _current; }
            constexpr pointer value() const { return _current; }

            constexpr overlapping_iterator_base &operator++()
            {
                advance();
                return *this;
            }

            constexpr overlapping_iterator_base operator++(int)
            {
                auto ret { *this };
                ++(*this);
                return ret;
            }

            friend constexpr bool operator==(const overlapping_iterator_base &lhs, const overlapping_iterator_base &rhs)
            {
                return (lhs._tree == rhs._tree && lhs._current == rhs._current) ||
                       (lhs._current == nullptr && rhs._current == nullptr);
            }

            friend constexpr bool operator!=(const overlapping_iterator_base &lhs, const overlapping_iterator_base &rhs)
            {
                return !(lhs == rhs);
            }
        };

        public:
        using iterator = typename rbtree<Type, Hook, less, augmentor>::iterator;
        using const_iterator = typename rbtree<Type, Hook, less, augmentor>::const_iterator;

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using overlapping_iterator = overlapping_iterator_base<Type>;
        using const_overlapping_iterator = overlapping_iterator_base<const Type>;

        constexpr interval_tree() : _rbtree { } { }

        constexpr interval_tree(const interval_tree &) = delete;
        constexpr interval_tree(interval_tree &&rhs) : _rbtree { std::move(rhs._rbtree) }
        { rhs._rbtree = { }; }

        constexpr interval_tree &operator=(const interval_tree &) = delete;
        constexpr interval_tree &operator=(interval_tree &&rhs)
        {
            if (this != &rhs)
            {
                _rbtree = std::move(rhs._rbtree);
                rhs._rbtree = { };
            }
            return *this;
        }

        constexpr void insert(Type *node)
        {
            bug_on(lower(node) > upper(node));
            ihook(node)->subtree_max = upper(node);
            _rbtree.insert(node);
        }

        constexpr void remove(Type *node)
        {
            _rbtree.remove(node);
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

        constexpr auto overlapping(IType lb, IType ub)
        {
            bug_on(lb >= ub);
            return std::ranges::subrange(
                overlapping_iterator { this, _rbtree.root(), lb, ub },
                overlapping_iterator { }
            );
        }

        constexpr auto overlapping(IType lb, IType ub) const
        {
            bug_on(lb >= ub);
            return std::ranges::subrange(
                const_overlapping_iterator { this, _rbtree.root(), lb, ub },
                const_overlapping_iterator { }
            );
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

    template<
        typename Type, typename IType,
        IType Type::*Lower, IType Type::*Upper,
        rbtree_hook<Type> Type::*Hook, interval_hook<IType> Type::*IHook
    >
    class interval_tree_alloc
    {
        private:
        interval_tree<Type, IType, Lower, Upper, Hook, IHook> _itree;

        constexpr void _delete_subtree(Type *node)
        {
            if (!node)
                return;

            auto h = &(node->*Hook);
            _delete_subtree(h->left);
            _delete_subtree(h->right);
            delete node;
        }

        public:
        using iterator = typename interval_tree<Type, IType, Lower, Upper, Hook, IHook>::iterator;
        using const_iterator = typename interval_tree<Type, IType, Lower, Upper, Hook, IHook>::const_iterator;

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using overlapping_iterator = typename interval_tree<Type, IType, Lower, Upper, Hook, IHook>::overlapping_iterator;
        using const_overlapping_iterator = typename interval_tree<Type, IType, Lower, Upper, Hook, IHook>::const_overlapping_iterator;

        constexpr interval_tree_alloc() : _itree { } { }

        constexpr interval_tree_alloc(const interval_tree_alloc &) = delete;
        constexpr interval_tree_alloc(interval_tree_alloc &&rhs) : _itree { std::move(rhs._itree) }
        { rhs._itree = { }; }

        constexpr ~interval_tree_alloc() { clear(); }

        constexpr interval_tree_alloc &operator=(const interval_tree_alloc &) = delete;
        constexpr interval_tree_alloc &operator=(interval_tree_alloc &&rhs)
        {
            if (this != &rhs)
            {
                clear();
                _itree = std::move(rhs._itree);
                rhs._itree = { };
            }
            return *this;
        }

        template<typename... Args>
        constexpr iterator emplace(Args&&... args)
        {
            auto obj = new Type(std::forward<Args>(args)...);
            _itree.insert(obj);
            return iterator { &_itree._rbtree, obj };
        }

        constexpr void remove(Type *x)
        {
            _itree.remove(x);
            delete x;
        }

        constexpr iterator remove(iterator x)
        {
            bug_on(x._tree != &_itree._rbtree);
            auto next = x;
            ++next;
            remove(x.value());
            return next;
        }

        constexpr const_iterator remove(const_iterator x)
        {
            bug_on(x._tree != &_itree._rbtree);
            auto next = x;
            ++next;
            remove(const_cast<Type *>(x.value()));
            return next;
        }

        constexpr void clear()
        {
            if (_itree.empty())
                return;
            _delete_subtree(_itree._rbtree.root());
            _itree = { };
        }

        constexpr auto overlapping(IType lb, IType ub)
        {
            return _itree.overlapping(lb, ub);
        }

        constexpr auto overlapping(IType lb, IType ub) const
        {
            return _itree.overlapping(lb, ub);
        }

        constexpr iterator begin() { return _itree.begin(); }
        constexpr iterator end() { return _itree.end(); }

        constexpr const_iterator begin() const { return _itree.begin(); }
        constexpr const_iterator end() const { return _itree.end(); }

        constexpr const_iterator cbegin() const { return _itree.cbegin(); }
        constexpr const_iterator cend() const { return _itree.cend(); }

        constexpr reverse_iterator rbegin() { return reverse_iterator { end() }; }
        constexpr reverse_iterator rend() { return reverse_iterator { begin() }; }

        constexpr const_reverse_iterator rbegin() const { return const_reverse_iterator { end() }; }
        constexpr const_reverse_iterator rend() const { return const_reverse_iterator { begin() }; }

        constexpr const_reverse_iterator rcbegin() const { return const_reverse_iterator { end() }; }
        constexpr const_reverse_iterator rcend() const { return const_reverse_iterator { begin() }; }

        constexpr std::size_t size() const { return _itree.size(); }
        constexpr bool empty() const { return _itree.empty(); }
    };
} // export namespace lib
