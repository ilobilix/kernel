// Copyright (C) 2024-2026  ilobilo

export module nvme:cmd;

import system.sched.wait_queue;
import libarch;
import lib;

import :spec;

export namespace nvme
{
    class command_t
    {
        public:
        using result = std::pair<spec::completion_status_t, spec::completion_entry_t::result_t>;

        private:
        sched::wait_queue_t _queue;
        spec::command_t _cmd;

        std::atomic_bool _done;
        spec::completion_status_t _status;
        spec::completion_entry_t::result_t _result;

        arch::dma_pool &_pool;
        lib::list<arch::dma_array<std::uint64_t>> _prps;
        arch::dma_buffer _buf;

        std::optional<sched::wait_queue_entry_t> _async_entry;
        std::function<void (result)> _async_cb;

        public:
        command_t(arch::dma_pool &pool)
            : _queue { }, _cmd { }, _done { false }, _status { }, _result { },
              _pool { pool }, _prps { }, _buf { } { }

        void setup(arch::dma_buffer_view view);

        void own(arch::dma_buffer buf) { _buf = std::move(buf); }

        spec::command_t &buffer() { return _cmd; }

        bool done() const { return _done.load(std::memory_order_acquire); }
        result get() const { return { _status, _result }; }

        bool wait(std::uint64_t ns = 0)
        {
            while (true)
            {
                const auto gen = _queue.snapshot_gen();
                if (done())
                    return true;
                const auto res = _queue.wait_unkillable_prepared(gen, ns);
                if (ns != 0 && res.expired)
                    return done();
            }
        }

        void on_complete(std::function<void (result)> cb)
        {
            _async_cb = std::move(cb);
            _async_entry.emplace([this] { _async_cb(get()); });
            _queue.add_entry(*_async_entry);
        }

        void complete(const spec::completion_entry_t &entry)
        {
            _status = entry.status;
            _result = entry.result;
            _done.store(true, std::memory_order_release);
            _queue.wake_all();
        }
    };
} // export namespace nvme
