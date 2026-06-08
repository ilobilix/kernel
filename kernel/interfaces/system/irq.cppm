// Copyright (C) 2024-2026  ilobilo

export module system.irq;

import system.cpu.regs;
import lib;
import std;

export namespace irq
{
    using handler_fn = std::function<void(cpu::registers *)>;

    using handle_t = std::uint32_t;
    constexpr handle_t invalid_handle = 0;

    enum class trigger : std::uint8_t
    {
        none,
        edge_rising,
        edge_falling,
        edge_both,
        level_high,
        level_low,
    };

    struct fwspec
    {
        std::uint32_t param_count = 0;
        std::uint32_t params[4] { };
    };

    struct msi_msg
    {
        std::uint64_t address;
        std::uint32_t data;
    };

    struct domain;
    struct irq_data
    {
        std::uint32_t virq = 0;
        std::uintptr_t hwirq = 0;
        std::uintptr_t aux = 0;

        trigger trig = trigger::none;
        domain *dom = nullptr;
        irq_data *parent = nullptr;
    };

    struct domain
    {
        std::string_view name;
        domain *parent = nullptr;

        domain(std::string_view name, domain *parent = nullptr) : name { name }, parent { parent } { }

        virtual lib::expect<void> alloc(std::span<irq_data *> data, const fwspec &spec) = 0;

        virtual void free(std::span<irq_data *> data) = 0;

        virtual void attach(irq_data &data, handler_fn *fn)
        {
            if (parent && data.parent)
                parent->attach(*data.parent, fn);
        }

        virtual void detach(irq_data &data)
        {
            if (parent && data.parent)
                parent->detach(*data.parent);
        }

        virtual void mask(irq_data &data) { lib::unused(data); }
        virtual void unmask(irq_data &data) { lib::unused(data); }

        virtual void eoi(irq_data &data) { lib::unused(data); }
        virtual lib::expect<void> set_affinity(irq_data &data, const lib::bitmap &cpus, bool force)
        {
            if (parent && data.parent)
                return parent->set_affinity(*data.parent, cpus, force);
            return std::unexpected { lib::err::not_supported };
        }

        virtual lib::expect<msi_msg> compose_msi(irq_data &data)
        {
            lib::unused(data);
            return std::unexpected { lib::err::not_supported };
        }

        virtual ~domain() = default;
    };

    lib::expect<handle_t> alloc(domain &leaf, const fwspec &spec);
    void free(handle_t handle);

    lib::expect<std::vector<handle_t>> alloc_num(
        domain &leaf, const fwspec &spec, std::size_t count
    );

    lib::expect<void> request(handle_t handle, handler_fn fn, std::string_view name = { });

    lib::expect<handle_t> alloc_and_request(
        domain &leaf, const fwspec &spec, handler_fn fn, std::string_view name = { }
    );

    void mask(handle_t handle);
    void unmask(handle_t handle);

    lib::expect<void> set_affinity(handle_t handle, const lib::bitmap &cpus, bool force = false);

    lib::expect<msi_msg> compose_msi(handle_t handle);

    domain *msi_parent();
    void set_msi_parent(domain *parent);

    using gsi_requester_fn = lib::expect<handle_t> (*)(
        std::uint32_t gsi, trigger trig, std::size_t cpu_idx, handler_fn fn, std::string_view name
    );
    void set_gsi_requester(gsi_requester_fn fn);

    lib::expect<handle_t> request_gsi(
        std::uint32_t gsi, trigger trig, std::size_t cpu_idx, handler_fn fn,
        std::string_view name = { }
    );
} // export namespace irq
