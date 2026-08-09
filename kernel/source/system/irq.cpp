// Copyright (C) 2024-2026  ilobilo

module system.irq;

import system.cpu.call;
import system.cpu;
import system.sched;
import system.rcu;
import frigg;

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

            bool requested = false;
            bool dead = false;
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

        lib::locker<
            frg::manual_box<
                cpu::batch_t<std::monostate>
            >, sched::mutex_t
        > batch;

        void wait_for_handlers()
        {
            if (!sched::is_running() || cpu::count() <= 1)
                return;

            auto locked = batch.lock();
            if (!locked->valid())
                locked->initialize();
            auto &batch = *locked;

            sched::preempt_disable();
            std::atomic_thread_fence(std::memory_order_seq_cst);

            batch->build([](std::size_t, auto &) { return true; });
            const bool none = batch->empty();
            if (!none)
                batch->dispatch([](cpu::call_t *) { return true; });

            sched::preempt_enable();
            if (!none)
                batch->wait("irq handler drain");
        }

        std::shared_ptr<desc_t> lookup(handle_t handle)
        {
            auto locked = descs.lock();
            const auto it = locked->find(handle);
            if (it == locked->end())
                return nullptr;
            return it->second;
        }

        struct detach_t
        {
            actions_t *old = nullptr;
            bool teardown = false;
            bool changed = false;
        };

        detach_t detach_actions(desc_t &desc, const void *owner)
        {
            detach_t ret;

            const std::unique_lock _ { desc.lock };
            if (desc.dead)
                return ret;

            ret.old = desc.actions.load(std::memory_order_relaxed);
            if (ret.old != nullptr)
            {
                auto next = std::make_unique<actions_t>();
                for (const auto &act : *ret.old)
                {
                    if (act.owner != owner)
                        next->push_back(act);
                }

                if (next->size() == ret.old->size())
                    return { };

                ret.teardown = next->empty();
                desc.actions.store(
                    ret.teardown ? nullptr : next.release(), std::memory_order_release
                );
            }
            else ret.teardown = true;

            if (ret.teardown)
                desc.dead = true;

            ret.changed = true;
            return ret;
        }

        bool unhook(desc_t &desc, handle_t handle)
        {
            descs.lock()->erase(handle);
            if (!desc.requested)
                return false;

            desc.leaf.dom->mask(desc.leaf);
            desc.leaf.dom->detach(desc.leaf);
            return true;
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

    lib::expect<std::vector<handle_t>> alloc(domain &leaf, const fwspec &spec, std::size_t count)
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

        const auto ret = detach_actions(*desc, owner);
        if (!ret.changed)
            return;

        if (!ret.teardown)
        {
            ret.old->retire();
            return;
        }

        if (unhook(*desc, handle))
            wait_for_handlers();

        if (ret.old != nullptr)
            ret.old->retire();

        auto node = &desc->leaf;
        desc->leaf.dom->free({ &node, 1 });
    }

    void free(std::span<const handle_t> handles, const void *owner)
    {
        if (handles.empty())
            return;

        std::vector<std::shared_ptr<desc_t>> dying;
        std::vector<actions_t *> stale;

        dying.reserve(handles.size());
        stale.reserve(handles.size());

        bool detached = false;
        for (const auto handle : handles)
        {
            auto desc = lookup(handle);
            if (!desc)
                continue;

            const auto ret = detach_actions(*desc, owner);
            if (!ret.changed)
                continue;

            if (ret.old != nullptr)
                stale.push_back(ret.old);

            if (!ret.teardown)
                continue;

            detached |= unhook(*desc, handle);
            dying.push_back(std::move(desc));
        }

        if (detached)
            wait_for_handlers();

        for (auto old : stale)
            old->retire();

        if (dying.empty())
            return;

        std::ranges::sort(dying, { }, [](const auto &desc) { return desc->leaf.dom; });

        std::vector<irq_data *> nodes;
        nodes.reserve(dying.size());

        for (std::size_t i = 0; i < dying.size(); )
        {
            auto dom = dying[i]->leaf.dom;
            nodes.clear();

            while (i < dying.size() && dying[i]->leaf.dom == dom)
                nodes.push_back(&dying[i++]->leaf);

            dom->free(nodes);
        }
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
        handler_fn fn, std::string_view name, bool unmask, const void *owner
    )
    {
        auto handle = alloc(leaf, spec);
        if (!handle)
            return std::unexpected { handle.error() };

        if (auto ret = request(*handle, std::move(fn), name, unmask, owner); !ret)
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
        return desc->leaf.dom->set_affinity(desc->leaf, cpus, force);
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
        handler_fn fn, std::string_view name, bool unmask, const void *owner
    )
    {
        const auto requester = _gsi_requester.load(std::memory_order_acquire);
        if (!requester)
            return std::unexpected { lib::err::not_supported };
        return requester(gsi, trig, cpu_idx, std::move(fn), name, unmask, owner);
    }
} // namespace irq
