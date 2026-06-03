// Copyright (C) 2024-2026  ilobilo

export module system.pci:msix;

import system.irq;
import lib;
import std;

import :core;

export namespace pci::msix
{
    struct msix_domain : irq::domain
    {
        private:
        pci::device *_dev;
        std::uint16_t _cap_offset;
        std::uint16_t _nvec;
        std::uintptr_t _table;

        lib::bitmap _allocated;
        std::size_t _live_count;
        lib::spinlock _lock;

        public:
        enum fwparam : std::uint32_t
        {
            param_cpu = 0,
            param_count = 1
        };

        msix_domain(pci::device &dev, std::uint16_t cap_offset, irq::domain *parent);

        lib::expect<void> alloc(
            std::span<irq::irq_data *> data, const irq::fwspec &spec
        ) override;

        void free(std::span<irq::irq_data *> data) override;

        void mask(irq::irq_data &data) override;
        void unmask(irq::irq_data &data) override;

        lib::expect<void> set_affinity(
            irq::irq_data &data, const lib::bitmap &cpus, bool force
        ) override;

        std::size_t live_count() const { return _live_count; }
    };

    msix_domain *for_device(pci::device &dev);
    void release(pci::device &dev);

    bool is_enabled(const pci::device &dev);

    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx,
        irq::handler_fn fn, std::string_view name = { }
    );

    lib::expect<std::vector<irq::handle_t>> alloc(
        pci::device &dev, std::size_t count, std::size_t cpu_idx
    );
} // export namespace pci::msix
