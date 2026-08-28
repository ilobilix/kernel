// Copyright (C) 2024-2026  ilobilo

export module x86_64.system.vector;

import system.cpu.local;
import system.cpu.regs;
import system.irq;
import lib;
import std;

export namespace x86_64::vector
{
    constexpr std::size_t num_ints = 256;
    constexpr std::size_t num_preints = 20;

    constexpr std::uint8_t vec_spurious = 0xFF;
    constexpr std::uint8_t vec_lapic_error = 0xFE;
    constexpr std::uint8_t vec_sched = 0xFD;
    constexpr std::uint8_t vec_cpu_call = 0xFC;

    constexpr std::uint8_t irq(std::uint8_t num) { return num + 0x20; }

    struct slot_t
    {
        using flow_fn = void (*)(cpu::registers *, slot_t &);

        std::function<void (cpu::registers *)> handler;

        flow_fn flow = nullptr;
        std::uintptr_t flow_data = 0;

        bool reserved = false;

        bool used() const { return bool(handler); }
        bool is_reserved() const { return reserved; }

        void reserve() { reserved = true; }
        void set(std::function<void (cpu::registers *)> fn) { handler = std::move(fn); }

        void set_flow(flow_fn fn, std::uintptr_t data = 0)
        {
            flow = fn;
            flow_data = data;
        }

        void clear_flow()
        {
            flow = nullptr;
            flow_data = 0;
        }

        void reset()
        {
            handler = nullptr;
            clear_flow();
        }

        void reset_all()
        {
            handler = nullptr;
            clear_flow();
            reserved = false;
        }

        void operator()(cpu::registers *regs) const
        {
            if (handler)
                handler(regs);
        }
    };

    struct domain_t : irq::domain
    {
        enum fwparam : std::uint32_t
        {
            param_cpu = 0,
            param_hint = 1,
            param_count = 2
        };

        domain_t() : domain { "x86_64-vector" } { }

        lib::expect<void> alloc(
            std::span<irq::irq_data *> data, const irq::fwspec &spec
        ) override;

        void free(std::span<irq::irq_data *> data) override;

        void attach(irq::irq_data &data, irq::handler_fn *fn) override;
        void detach(irq::irq_data &data) override;

        lib::expect<void> set_affinity(
            irq::irq_data &data, lib::const_bitmap_view cpus, bool force
        ) override;

        lib::expect<irq::msi_msg> compose_msi(irq::irq_data &data) override;
    };

    domain_t *get_domain();

    [[nodiscard]]
    std::optional<slot_t &> handler_at(std::size_t cpuidx, std::uint8_t num);

    void dispatch(cpu::registers *regs);
    void init_on(cpu::processor *cpu);
} // namespace x86_64::vector
