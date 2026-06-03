// Copyright (C) 2024-2026  ilobilo

module system.irq;

import lib;
import std;

namespace irq
{
    namespace
    {
        struct desc_t
        {
            lib::spinlock_irq lock;
            std::string_view name;

            irq_data leaf;
            handler_fn handler;

            lib::bitmap affinity;
            bool requested = false;
        };

        lib::locker<
            lib::map::flat_hash<
                handle_t,
                std::shared_ptr<desc_t>
            >, lib::spinlock_irq
        > descs;

        std::atomic<handle_t> next_virq { 1 };

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

        if (auto ret = leaf.alloc({ &desc->leaf, 1 }, spec); !ret)
            return std::unexpected { ret.error() };

        const auto virq = desc->leaf.virq;
        descs.lock()->emplace(virq, std::move(desc));
        return virq;
    }

    void free(handle_t handle)
    {
        std::shared_ptr<desc_t> desc;
        {
            auto locked = descs.lock();
            const auto it = locked->find(handle);
            if (it == locked->end())
                return;
            desc = std::move(it->second);
            locked->erase(it);
        }

        if (desc->requested)
            desc->leaf.dom->detach(desc->leaf);

        desc->leaf.dom->free({ &desc->leaf, 1 });
    }

    lib::expect<void> request(handle_t handle, handler_fn fn, std::string_view name)
    {
        auto desc = lookup(handle);
        if (!desc)
            return std::unexpected { lib::err::not_found };

        const std::unique_lock _ { desc->lock };
        if (desc->requested)
            return std::unexpected { lib::err::already_exists };

        desc->name = name;
        desc->handler = std::move(fn);
        desc->leaf.dom->attach(desc->leaf, &desc->handler);
        desc->requested = true;
        desc->leaf.dom->unmask(desc->leaf);
        return { };
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

    lib::expect<handle_t> alloc_and_request(
        domain &leaf, const fwspec &spec,
        handler_fn fn, std::string_view name
    )
    {
        auto handle = alloc(leaf, spec);
        if (!handle)
            return std::unexpected { handle.error() };

        if (auto ret = request(*handle, std::move(fn), name); !ret)
        {
            free(*handle);
            return std::unexpected { ret.error() };
        }
        return *handle;
    }
} // namespace irq
