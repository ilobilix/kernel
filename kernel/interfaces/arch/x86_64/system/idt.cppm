// Copyright (C) 2024-2026  ilobilo

export module x86_64.system.idt;

import x86_64.system.gdt;
import system.cpu.local;
import system.cpu.regs;
import system.irq;
import lib;
import std;

export namespace x86_64::idt
{
    struct slot
    {
        using flow_fn = void (*)(cpu::registers *, slot &);

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

        void operator()(cpu::registers *regs)
        {
            if (handler)
                handler(regs);
        }
    };

    struct [[gnu::packed]] entry
    {
        std::uint16_t offset0;
        std::uint16_t selector;
        std::uint8_t ist;
        std::uint8_t typeattr;
        std::uint16_t offset1;
        std::uint32_t offset2;
        std::uint32_t zero;

        void set(void *isr, std::uint8_t _typeattr = 0x8E, std::uint8_t _ist = 0)
        {
            auto addr = reinterpret_cast<std::uintptr_t>(isr);

            offset0 = static_cast<std::uint16_t>(addr);
            offset1 = static_cast<std::uint16_t>(addr >> 16);
            offset2 = static_cast<std::uint32_t>(addr >> 32);

            selector = gdt::segment::code;
            ist = _ist;
            typeattr = _typeattr;

            zero = 0;
        }
    };

    struct [[gnu::packed]] reg
    {
        std::uint16_t limit;
        std::uint64_t base;

        void load() const
        {
            asm volatile ("cli; lidt %0" :: "memory"(*this));
        }
    };

    inline constexpr std::uint8_t irq(std::uint8_t num) { return num + 0x20; }

    constexpr std::size_t num_ints = 256;
    constexpr std::size_t num_preints = 20;

    constexpr std::uint8_t vec_spurious = 0xFF;
    constexpr std::uint8_t vec_lapic_error = 0xFE;
    constexpr std::uint8_t vec_sched = 0xFD;
    constexpr std::uint8_t vec_tlb_shootdown = 0xFC;

    constexpr reg invalid { 0, 0 };

    std::array<entry, num_ints> &table();

    [[nodiscard]]
    std::optional<slot &> handler_at(std::size_t cpuidx, std::uint8_t num);

    struct vector_domain : irq::domain
    {
        enum fwparam : std::uint32_t
        {
            param_cpu = 0,
            param_hint = 1,
            param_count = 2
        };

        vector_domain() : domain { "x86_64-vector" } { }

        lib::expect<void> alloc(
            std::span<irq::irq_data *> data, const irq::fwspec &spec
        ) override;

        void free(std::span<irq::irq_data *> data) override;

        void attach(irq::irq_data &data, irq::handler_fn *fn) override;
        void detach(irq::irq_data &data) override;

        lib::expect<void> set_affinity(
            irq::irq_data &data, const lib::bitmap &cpus, bool force
        ) override;

        lib::expect<irq::msi_msg> compose_msi(irq::irq_data &data) override;
    };

    vector_domain *get_vector_domain();

    void init();
    void init_on(cpu::processor *cpu);
} // namespace x86_64::idt
