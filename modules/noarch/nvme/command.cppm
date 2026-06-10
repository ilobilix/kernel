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
        private:
        sched::wait_queue_t _queue;
        spec::command_t _cmd;

        std::atomic_bool _done;
        spec::completion_status_t _status;
        spec::completion_entry_t::result_t _result;

        arch::dma_pool &_pool;
        lib::list<arch::dma_array<std::uint64_t>> _prps;
        arch::dma_buffer _buf;

        public:
        using result = std::pair<spec::completion_status_t, spec::completion_entry_t::result_t>;

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
            while (!done())
            {
                const auto res = _queue.wait_unkillable(ns);
                if (ns != 0 && res.expired)
                    return done();
            }
            return true;
        }

        void complete(const spec::completion_entry_t &entry)
        {
            _status = entry.status;
            _result = entry.result;
            _done.store(true, std::memory_order_release);
            _queue.wake_one();
        }
    };
} // export namespace nvme
