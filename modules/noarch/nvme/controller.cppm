// Copyright (C) 2024-2026  ilobilo

export module nvme:ctrl;

import system.sched;
import system.pci;
import system.irq;

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
        std::vector<irq::handle_t> _irq_handles;
        pci::irq_type _irq_type;

        std::vector<std::unique_ptr<queue_t>> _queues;
        std::vector<std::shared_ptr<namespace_t>> _namespaces;

        struct worker_t
        {
            static constexpr sched::nice_t nice = -20;

            controller_t *_ctrl;
            std::size_t _cpu;
            std::vector<std::uint32_t> _qids;
            std::uint32_t _mask;

            std::weak_ptr<sched::thread_t> _thread;
            sched::wait_queue_t _bell;
            std::atomic_bool _ack;

            static void worker(worker_t *self);
            void irq_handler();

            void start();

            ~worker_t();
        };
        lib::btree::map<
            std::size_t,
            std::unique_ptr<worker_t>
        > _workers;

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
        static lib::expect<std::shared_ptr<controller_t>> create(pci::device_t &dev);

        std::span<const std::shared_ptr<namespace_t>> namespaces() const { return _namespaces; }

        ~controller_t();
    };
} // export namespace nvme
