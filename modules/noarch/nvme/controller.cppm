// Copyright (C) 2024-2026  ilobilo

export module nvme:ctrl;

import system.sched;
import system.irq;
import drivers.pci;
import drivers.dev;

import :queue;
import :ns;

export namespace nvme
{
    class controller_t
    {
        private:
        static std::tuple<std::uint16_t, std::uint8_t, std::uint8_t> version(std::uint32_t ver)
        {
            return {
                static_cast<std::uint16_t>((ver >> 16) & 0xFFFF),
                static_cast<std::uint8_t>((ver >> 8) & 0xFF),
                static_cast<std::uint8_t>(ver & 0xFF)
            };
        }

        std::shared_ptr<pci::device> _dev;
        pci::irq_alloc_t _irqs;

        std::vector<std::unique_ptr<queue_t>> _queues;
        std::vector<std::shared_ptr<namespace_t>> _namespaces;

        struct worker_t
        {
            std::vector<std::uint32_t> qids;
            std::uint32_t mask;
            std::unique_ptr<sched::irq_worker_t> thread;
        };
        std::vector<std::unique_ptr<worker_t>> _workers;

        void drain(worker_t &worker);
        void irq_handler(worker_t &worker);

        arch::contiguous_pool _pool;

        arch::mem_space _regs;
        std::uint32_t _queue_depth;
        std::uint32_t _db_stride;
        std::uint32_t _version;
        std::uint32_t _toggle_wait_ms;

        std::size_t _max_transfer;
        bool _vwc;
        bool _enabled = false;

        std::unique_ptr<queue_t> &admin_queue() { return _queues[0]; }
        std::unique_ptr<queue_t> &io_queue(std::size_t cpu) { return _queues[cpu + 1]; }

        std::shared_ptr<command_t> create_cmd() { return std::make_shared<command_t>(_pool); }

        bool toggle(bool enable);
        lib::expect<void> init();

        controller_t(const std::shared_ptr<pci::device> &dev) : _dev { dev } { }

        public:
        std::shared_ptr<dev::kobject_t> dir;
        std::shared_ptr<dev::device_t> dev;

        static lib::expect<std::shared_ptr<controller_t>> create(
            const std::shared_ptr<pci::device> &dev
        );

        std::span<const std::shared_ptr<namespace_t>> namespaces() const { return _namespaces; }

        ~controller_t();
    };
} // export namespace nvme
