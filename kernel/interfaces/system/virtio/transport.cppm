// Copyright (C) 2024-2026  ilobilo

export module system.virtio:transport;

import lib;
import std;

import :spec;

export namespace virtio
{
    constexpr std::uint16_t no_vector = 0xFFFF;

    struct queue_addr_t
    {
        std::uintptr_t desc, avail, used;
        std::uint16_t size;
        std::uint16_t vector = no_vector;
    };

    using vector_fn = std::function<void (std::uint16_t vector, bool config)>;

    struct irq_layout_t
    {
        std::vector<std::uint16_t> queues;
        std::vector<std::size_t> cpus;
        std::uint16_t config = 0;
        std::uint16_t count = 0;
    };

    class transport_t
    {
        public:
        virtual std::string_view type() const = 0;

        virtual std::uint64_t device_features() = 0;
        virtual void driver_features(std::uint64_t feat) = 0;

        virtual std::uint64_t mandatory_features() const = 0;

        virtual std::uint8_t status() = 0;
        virtual void add_status(std::uint8_t bits) = 0;
        virtual void reset() = 0;

        virtual std::uint16_t num_queues() = 0;
        virtual std::uint16_t queue_max_size(std::uint16_t qid) = 0;

        virtual bool legacy_layout() const { return false; }

        virtual lib::expect<void> enable_queue(std::uint16_t qid, const queue_addr_t &addr) = 0;
        virtual void disable_queue(std::uint16_t qid) = 0;

        virtual void notify(std::uint16_t qid) = 0;

        virtual void read_config(std::size_t off, std::span<std::byte> buffer) = 0;
        virtual void write_config(std::size_t off, std::span<const std::byte> buffer) = 0;
        virtual std::uint8_t config_generation() = 0;

        virtual lib::expect<irq_layout_t> setup_irqs(
            std::uint16_t nqueues, std::size_t cpu, vector_fn on_trigger
        ) = 0;
        virtual void enable_irqs() = 0;
        virtual void release_irqs() = 0;
        virtual std::uint8_t isr_status() { return 0; }

        virtual ~transport_t() = default;
    };
} // export namespace virtio
