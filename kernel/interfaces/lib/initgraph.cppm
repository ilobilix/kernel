// Copyright (C) 2024-2026  ilobilo

export module lib:initgraph;

import :log;
import :intrusive;
import :bug_on;
import :panic;
import std;

// this code is yoinked from managarm

export namespace lib::initgraph
{
#if ILOBILIX_DEBUG
    constexpr bool debug = true;
#else
    constexpr bool debug = false;
#endif

    enum class node_type { none, stage, task };

    struct node;
    struct engine;
    struct edge;

    void print_mermaid();

    struct edge
    {
        friend struct node;
        friend struct engine;
        friend void realise_edge(edge *edge);
        friend void print_mermaid();

        private:
        node *_source;
        node *_target;

        lib::intrusive_list_hook<edge> _outhook;
        lib::intrusive_list_hook<edge> _inhook;

        public:
        edge(node *source, node *target)
            : _source { source }, _target { target } { realise_edge(this); }

        edge(const edge &) = delete;
        edge &operator=(const edge &) = delete;

        node *source() { return _source; }
        node *target() { return _target; }
    };

    struct node
    {
        friend struct engine;
        friend void realise_edge(edge *edge);
        friend void realise_node(node *node);
        friend void print_mermaid();

        private:
        node_type _type;
        engine *_engine;
        bool _mvisited;
        std::string_view _name;

        lib::intrusive_list<edge, &edge::_outhook> _outlist;
        lib::intrusive_list<edge, &edge::_inhook> _inlist;

        lib::intrusive_list_hook<node> _nodeshook;
        lib::intrusive_list_hook<node> _queuehook;

        bool _wanted = false;
        bool _done = false;

        std::uint32_t unsatisfied = 0;

        protected:
        virtual void activate() { }
        ~node() = default;

        public:
        node(node_type type, engine *engine, std::string_view name = "")
            : _type { type }, _engine { engine }, _mvisited { false }, _name { name } { realise_node(this); }

        node(const node &) = delete;
        node &operator=(const node &) = delete;

        node_type type() const { return _type; }
        engine *engine() { return _engine; }

        std::string_view name() const { return _name; }
    };

    struct engine final
    {
        friend void realise_edge(edge *edge);
        friend void realise_node(node *node);
        friend void print_mermaid();

        private:
        lib::intrusive_list<node, &node::_nodeshook> _nodes;
        lib::intrusive_list<node, &node::_queuehook> _pending;

        std::string_view _name;

        protected:
        void on_realise_node(initgraph::node *node)
        {
            if (node->type() == initgraph::node_type::stage)
                lib::debug("initgraph: registering stage '{}'", node->name());
            else if (node->type() == initgraph::node_type::task)
                lib::debug("initgraph: registering task '{}'", node->name());
        }

        // void on_realise_edge(initgraph::edge *edge)
        // {
        // }

        void pre_activate(initgraph::node *node)
        {
            if (node->type() == initgraph::node_type::task)
                lib::debug("initgraph: running task '{}'", node->name());
        }

        void post_activate(initgraph::node *node)
        {
            if (node->type() == initgraph::node_type::stage)
                lib::debug("initgraph: reached stage '{}'", node->name());
        }

        void report_unreached(initgraph::node *node)
        {
            if (node->type() == initgraph::node_type::stage)
                lib::debug("initgraph: stage '{}' could not be reached", node->name());
        }

        void on_unreached()
        {
            lib::panic("initgraph: unreached initialisation nodes (circular dependencies?)");
        }

        public:
        constexpr engine(std::string_view name) : _name { name } { }
        ~engine() = default;

        void run()
        {
            if constexpr (debug)
                lib::debug("initgraph: running engine '{}'", _name);

            for (auto &node : _nodes)
                node._wanted = true;

            for (auto &node : _nodes)
            {
                if (!node._wanted || node._done)
                    continue;

                if (node.unsatisfied == 0)
                    _pending.push_back(std::addressof(node));
            }

            while (!_pending.empty())
            {
                auto current = _pending.pop_front();
                lib::bug_on(current->_wanted == false || current->_done == true);

                if constexpr (debug)
                    pre_activate(current);

                current->activate();
                current->_done = true;

                if constexpr (debug)
                    post_activate(current);

                for (auto &edge : current->_outlist)
                {
                    auto successor = edge._target;
                    lib::bug_on(successor->unsatisfied == 0);
                    successor->unsatisfied--;

                    if (successor->_wanted && !successor->_done && successor->unsatisfied == 0)
                        _pending.push_back(successor);
                }
            }

            std::uint32_t unreached = 0;
            for (auto &node : _nodes)
            {
                if (!node._wanted || node._done)
                    continue;

                report_unreached(std::addressof(node));
                unreached++;
            }

            if (unreached != 0)
                on_unreached();

            if constexpr (debug)
                lib::debug("initgraph: finished running engine '{}'", _name);
        }
    };

    constinit engine presched_init_engine { "presched-engine" };
    constinit engine postsched_init_engine { "postsched-engine" };

    void print_mermaid()
    {
        static std::array engines {
            std::ref(presched_init_engine),
            std::ref(postsched_init_engine)
        };

        lib::println("flowchart TD");

        auto print_engine_nodes = [&](engine &eng, std::string_view label)
        {
            lib::println("  subgraph {}", label);
            for (const auto &node : eng._nodes)
            {
                if (node.type() == initgraph::node_type::task)
                    lib::println("    {}", node.name());
            }
            lib::println("  end");
        };

        auto print_engine_edges = [&](engine &eng)
        {
            for (auto &start_node : eng._nodes)
            {
                if (start_node.type() != initgraph::node_type::task)
                    continue;

                for (auto &engine : engines)
                {
                    for (auto &n : engine.get()._nodes)
                        n._mvisited = false;
                }

                start_node._mvisited = true;

                auto traverse = [&](this auto &self, initgraph::node *curr) -> void
                {
                    for (auto &edge : curr->_outlist)
                    {
                        auto next = edge._target;
                        if (next->_mvisited)
                            continue;
                        next->_mvisited = true;

                        if (next->type() == initgraph::node_type::task)
                            lib::println("  {} --> {}", start_node.name(), next->name());
                        else if (next->type() == initgraph::node_type::stage)
                            self(next);
                    }
                };
                traverse(&start_node);
            }
        };

        for (auto &engine : engines)
            print_engine_nodes(engine.get(), engine.get()._name);

        for (auto &engine : engines)
            print_engine_edges(engine.get());
    }

    inline void realise_node(node *node)
    {
        node->engine()->_nodes.push_back(node);

        if constexpr (debug)
            node->engine()->on_realise_node(node);
    }

    inline void realise_edge(edge *edge)
    {
        edge->_source->_outlist.push_back(edge);
        edge->_target->_inlist.push_back(edge);
        edge->_target->unsatisfied++;

        // if constexpr (debug)
        //     edge->source()->engine()->on_realise_edge(edge);
    }

    struct stage final : public node
    {
        stage(std::string_view name, struct engine &eng)
            : node { node_type::stage, &eng, name } { }
    };

    template<std::size_t N>
    struct require
    {
        std::array<node *, N> array;

        template<std::convertible_to<node *> ...Args>
        require(Args &&...args) : array { args... } { }

        require(const require &) = default;
    };

    template<typename ...Args>
    require(Args &&...) -> require<sizeof...(Args)>;

    template<std::size_t N>
    struct entail
    {
        std::array<node *, N> array;

        template<std::convertible_to<node *> ...Args>
        entail(Args &&...args) : array { args... } { }

        entail(const entail &) = default;
    };

    template<typename ...Args>
    entail(Args &&...) -> entail<sizeof...(Args)>;

    struct into_edges_to
    {
        node *target;

        template<typename ...Args>
        std::array<edge, sizeof...(Args)> operator()(Args &&...args) const
        {
            return { { { args, target } ... } };
        }
    };

    struct into_edges_from
    {
        node *source;

        template<typename ...Args>
        std::array<edge, sizeof...(Args)> operator()(Args &&...args) const
        {
            return { { { source, args } ... } };
        }
    };

    template<std::size_t ...S, typename Type, std::size_t N, typename Inv>
    auto apply(std::index_sequence<S...>, std::array<Type, N> array, Inv invocable)
    {
        return invocable(array[S]...);
    }

    template<typename Func, std::size_t NR = 0, std::size_t NE = 0>
    struct task final : node
    {
        private:
        Func _invocable;
        std::array<edge, NR> _redges;
        std::array<edge, NE> _eedges;

        protected:
        void activate() override { _invocable(); }

        public:
        task(std::string_view name, struct engine &eng, require<NR> r, entail<NE> e, Func invocable)
            : node { node_type::task, &eng, name }, _invocable { std::move(invocable) },
              _redges { apply(std::make_index_sequence<NR> { }, r.array, into_edges_to { this }) },
              _eedges { apply(std::make_index_sequence<NE> { }, e.array, into_edges_from { this }) } { }

        task(std::string_view name, struct engine &eng, Func invocable)
            : task { name, eng, { }, { }, std::move(invocable) } { }

        task(std::string_view name, struct engine &eng, require<NR> r, Func invocable)
            : task { name, eng, r, { }, std::move(invocable) } { }

        task(std::string_view name, struct engine &eng, entail<NE> e, Func invocable)
            : task { name, eng, { }, e, std::move(invocable) } { }
    };

    stage *base_stage()
    {
        static stage stage
        {
            "base-stage",
            presched_init_engine
        };
        return &stage;
    }
} // export namespace lib::initgraph