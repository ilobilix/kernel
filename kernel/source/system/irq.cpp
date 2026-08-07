// Copyright (C) 2024-2026  ilobilo

module system.irq;

import system.rcu;

namespace irq
{
    namespace
    {
        struct action_t
        {
            handler_fn fn;
            std::string_view name;
            const void *owner;
        };
        using actions_t = rcu::box<std::vector<action_t>>;

        struct desc_t
        {
            lib::spinlock_irq lock;

            irq_data leaf;
            std::atomic<actions_t *> actions = nullptr;
            handler_fn dispatch;

            lib::bitmap affinity;
            bool requested = false;
            bool dead = false;

            ~desc_t() { delete actions.load(std::memory_order_relaxed); }
        };

        lib::locker<
            lib::map::flat_hash<
                handle_t,
                std::shared_ptr<desc_t>
            >, lib::spinlock_irq
        > descs;

        std::atomic<handle_t> next_virq { 1 };

        std::atomic<domain *> _msi_parent { nullptr };
        std::atomic<gsi_requester_fn> _gsi_requester { nullptr };

        std::shared_ptr<desc_t> lookup(handle_t handle)
        {
            auto locked = descs.lock();
            const auto it = locked->find(handle);
            if (it == locked->end())
                return nullptr;
            return it->second;
        }
    } // namespace

    lib::expect<handle_t> alloc(domain &leaf, const fwspec &spec)
    {
        auto desc = std::make_shared<desc_t>();
        desc->leaf.virq = next_virq.fetch_add(1, std::memory_order_relaxed);
        desc->leaf.dom = &leaf;

        auto node = &desc->leaf;
        if (auto ret = leaf.alloc({ &node, 1 }, spec); !ret)
            return std::unexpected { ret.error() };

        const auto virq = desc->leaf.virq;
        descs.lock()->emplace(virq, std::move(desc));
        return virq;
    }

    lib::expect<std::vector<handle_t>> alloc_num(
        domain &leaf, const fwspec &spec, std::size_t count
    )
    {
        if (count == 0)
            return { };

        std::vector<std::shared_ptr<desc_t>> built;
        std::vector<irq_data *> nodes;

        built.reserve(count);
        nodes.reserve(count);

        for (std::size_t i = 0; i < count; i++)
        {
            auto desc = std::make_shared<desc_t>();
            desc->leaf.virq = next_virq.fetch_add(1, std::memory_order_relaxed);
            desc->leaf.dom = &leaf;

            nodes.push_back(&desc->leaf);
            built.push_back(std::move(desc));
        }

        if (auto ret = leaf.alloc(nodes, spec); !ret)
            return std::unexpected { ret.error() };

        std::vector<handle_t> handles;
        handles.reserve(count);

        auto locked = descs.lock();
        for (auto &desc : built)
        {
            handles.push_back(desc->leaf.virq);
            locked->emplace(desc->leaf.virq, std::move(desc));
        }

        return handles;
    }

    void free(handle_t handle, const void *owner)
    {
        auto desc = lookup(handle);
        if (!desc)
            return;

        actions_t *old = nullptr;
        bool teardown = false;
        {
            const std::unique_lock _ { desc->lock };
            if (desc->dead)
                return;

            old = desc->actions.load(std::memory_order_relaxed);
            if (old != nullptr)
            {
                auto next = std::make_unique<actions_t>();
                for (const auto &act : *old)
                {
                    if (act.owner != owner)
                        next->push_back(act);
                }

                if (next->size() == old->size())
                    return;

                teardown = next->empty();
                desc->actions.store(teardown ? nullptr : next.release(), std::memory_order_release);
            }
            else teardown = true;

            if (teardown)
                desc->dead = true;
        }

        if (old != nullptr)
            old->retire();

        if (!teardown)
        {
            rcu::synchronise();
            return;
        }

        descs.lock()->erase(handle);

        if (desc->requested)
        {
            desc->leaf.dom->mask(desc->leaf);
            desc->leaf.dom->detach(desc->leaf);
            rcu::synchronise();
        }

        auto node = &desc->leaf;
        desc->leaf.dom->free({ &node, 1 });
    }

    lib::expect<void> request(
        handle_t handle, handler_fn fn, std::string_view name,
        bool unmask, const void *owner
    )
    {
        if (!fn)
            return std::unexpected { lib::err::invalid_argument };

        auto desc = lookup(handle);
        if (!desc)
            return std::unexpected { lib::err::not_found };

        actions_t *old = nullptr;
        {
            const std::unique_lock _ { desc->lock };

            if (desc->dead)
                return std::unexpected { lib::err::not_found };

            old = desc->actions.load(std::memory_order_relaxed);
            if (old != nullptr)
            {
                for (const auto &act : *old)
                {
                    if (act.owner == owner)
                        return std::unexpected { lib::err::already_exists };
                }
            }

            auto next = old != nullptr
                ? std::make_unique<actions_t>(*old)
                : std::make_unique<actions_t>();
            next->emplace_back(std::move(fn), name, owner);

            desc->actions.store(next.release(), std::memory_order_release);

            if (!desc->requested)
            {
                desc->dispatch = [raw = desc.get()](cpu::registers *regs) {
                    if (const auto *cur = raw->actions.load(std::memory_order_acquire))
                    {
                        for (const auto &act : *cur)
                            act.fn(regs);
                    }
                };

                desc->leaf.dom->attach(desc->leaf, &desc->dispatch);
                desc->requested = true;
            }

            if (unmask)
                desc->leaf.dom->unmask(desc->leaf);
        }

        if (old != nullptr)
            old->retire();
        return { };
    }

    lib::expect<std::uintptr_t> hwirq_of(handle_t handle, const domain *expected)
    {
        auto desc = lookup(handle);
        if (!desc)
            return std::unexpected { lib::err::not_found };

        if (expected != nullptr && desc->leaf.dom != expected)
            return std::unexpected { lib::err::invalid_argument };
        return desc->leaf.hwirq;
    }

    lib::expect<handle_t> alloc_and_request(
        domain &leaf, const fwspec &spec,
        handler_fn fn, std::string_view name, const void *owner
    )
    {
        auto handle = alloc(leaf, spec);
        if (!handle)
            return std::unexpected { handle.error() };

        if (auto ret = request(*handle, std::move(fn), name, true, owner); !ret)
        {
            free(*handle, owner);
            return std::unexpected { ret.error() };
        }
        return *handle;
    }

    void mask(handle_t handle)
    {
        if (auto desc = lookup(handle))
            desc->leaf.dom->mask(desc->leaf);
    }

    void unmask(handle_t handle)
    {
        if (auto desc = lookup(handle))
            desc->leaf.dom->unmask(desc->leaf);
    }

    lib::expect<void> set_affinity(handle_t handle, const lib::bitmap &cpus, bool force)
    {
        auto desc = lookup(handle);
        if (!desc)
            return std::unexpected { lib::err::not_found };

        const std::unique_lock _ { desc->lock };
        if (auto ret = desc->leaf.dom->set_affinity(desc->leaf, cpus, force); !ret)
            return ret;

        desc->affinity = cpus;
        return { };
    }

    lib::expect<msi_msg> compose_msi(handle_t handle)
    {
        auto desc = lookup(handle);
        if (!desc)
            return std::unexpected { lib::err::not_found };
        return desc->leaf.dom->compose_msi(desc->leaf);
    }

    domain *msi_parent()
    {
        return _msi_parent.load(std::memory_order_acquire);
    }

    void set_msi_parent(domain *parent)
    {
        _msi_parent.store(parent, std::memory_order_release);
    }

    void set_gsi_requester(gsi_requester_fn fn)
    {
        _gsi_requester.store(fn, std::memory_order_release);
    }

    lib::expect<handle_t> request_gsi(
        std::uint32_t gsi, trigger trig, std::size_t cpu_idx,
        handler_fn fn, std::string_view name, const void *owner
    )
    {
        const auto requester = _gsi_requester.load(std::memory_order_acquire);
        if (!requester)
            return std::unexpected { lib::err::not_supported };
        return requester(gsi, trig, cpu_idx, std::move(fn), name, owner);
    }
} // namespace irq
