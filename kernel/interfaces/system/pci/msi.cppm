// Copyright (C) 2024-2026  ilobilo

export module system.pci:msi;

import system.irq;
import lib;
import std;

import :core;

export namespace pci::msi
{
    struct msi_domain : irq::domain
    {
        private:
        pci::device *_dev;
        std::uint16_t _cap_offset;

        bool _addr64;
        bool _per_vector_mask;

        std::uint8_t _allocated_vecs;
        std::vector<irq::irq_data *> _spare_parents;

        lib::spinlock _lock;

        void _mask(std::uintptr_t hwirq);
        void _unmask(std::uintptr_t hwirq);

        public:
        enum fwparam : std::uint32_t
        {
            param_cpu = 0,
            param_count = 1
        };

        msi_domain(pci::device &dev, std::uint16_t cap_offset, irq::domain *parent);
        ~msi_domain() override;

        lib::expect<void> alloc(std::span<irq::irq_data *> data, const irq::fwspec &spec) override;

        void free(std::span<irq::irq_data *> data) override;

        void mask(irq::irq_data &data) override;
        void unmask(irq::irq_data &data) override;

        lib::expect<void> set_affinity(
            irq::irq_data &data, const lib::bitmap &cpus, bool force
        ) override;

        std::size_t live_count() const { return _allocated_vecs; }
    };

    msi_domain *for_device(pci::device &dev);
    void release(pci::device &dev);

    bool is_enabled(const pci::device &dev);

    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx, irq::handler_fn fn, std::string_view name = { }
    );

    lib::expect<std::vector<irq::handle_t>> alloc(
        pci::device &dev, std::size_t count, std::size_t cpu_idx
    );
} // export namespace pci::msi
