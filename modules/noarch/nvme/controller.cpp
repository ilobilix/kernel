// Copyright (C) 2024-2026  ilobilo

module nvme;

import system.memory.phys;
import system.chrono;
import system.cpu;
import magic_enum;
import arch;

namespace nvme
{
    void controller_t::worker_t::worker(worker_t *self)
    {
        while (true)
        {
            if (self->_bell.wait().killed)
            {
                self->_ack.store(true, std::memory_order_release);
                sched::thread_exit(0);
            }

            for (const auto &qid : self->_qids)
            {
                lib::bug_on(self->_ctrl->_queues.size() <= qid);
                self->_ctrl->_queues[qid]->process();
            }

            if (self->_mask)
                self->_ctrl->_regs.store(regs::intmc, self->_mask);
        }
    }

    void controller_t::worker_t::irq_handler()
    {
        if (_mask)
            _ctrl->_regs.store(regs::intms, _mask);
        _bell.wake_one();
    }

    void controller_t::worker_t::start()
    {
        lib::bug_on(!_thread.expired());

        auto thread = sched::create_kthread(
            reinterpret_cast<std::uintptr_t>(worker),
            reinterpret_cast<std::uintptr_t>(this),
            worker_t::nice
        );
        thread->affinity.clear();
        thread->affinity.set(_cpu, true);

        sched::enqueue_new(thread.get());
        _thread = std::move(thread);
    }

    controller_t::worker_t::~worker_t()
    {
        if (auto thread = _thread.lock())
        {
            sched::request_kill(thread.get(), 0);
            while (!_ack.load(std::memory_order_acquire))
                ::arch::pause();
        }
    }

    bool controller_t::toggle(bool enable)
    {
        const auto cc = _regs.load(regs::cc);
        _regs.store(regs::cc, cc / flags::cc::enable(enable));

        const auto clock = chrono::main_timer();
        const auto end = clock->ns() + (_toggle_wait_ms * 1'000'000ul);

        const auto rdy = enable ? flags::csts::rdy : 0;

        do {
            const auto csts = _regs.load(regs::csts);
            if ((csts & flags::csts::rdy) == rdy)
                return true;
            if (csts & flags::csts::cfs)
                return false;

            arch::pause();
        } while (clock->ns() < end);

        const auto csts = _regs.load(regs::csts);
        return ((csts & flags::csts::rdy) == rdy) && !(csts & flags::csts::cfs);
    }

    lib::expect<void> controller_t::init()
    {
        _dev->write<16>(pci::reg::cmd, _dev->read<16>(pci::reg::cmd) |
            pci::mem_space | pci::bus_master | pci::int_dis
        );

        auto &bar0 = _dev->get_bars()[0];
        if (bar0.type != pci::bar::type::mem || !bar0.bits64)
        {
            lib::error("nvme: invalid bar");
            return std::unexpected { lib::err::invalid_argument };
        }

        _regs = bar0.map();

        const auto [maj, min, ter] = version(_version = _regs.load(regs::vs));
        lib::info("nvme: controller version: {}.{}.{}", maj, min, ter);

        const auto cap = _regs.load(regs::cap);

        _queue_depth = std::min((cap & flags::cap::mqes) + 1u, max_queue_depth);
        _db_stride = 1u << (cap & flags::cap::dstrd);
        _toggle_wait_ms = (cap & flags::cap::to) * 500;

        if (!toggle(false))
        {
            lib::error("nvme: failed to disable controller");
            return std::unexpected { lib::err::io_error };
        }

        const auto doorbells_for = [&](std::uint32_t qid) {
            const auto stride = 4 * _db_stride;
            return std::pair {
                _regs.subspace(0x1000 + (2 * qid) * stride),
                _regs.subspace(0x1000 + (2 * qid + 1) * stride)
            };
        };

        {
            auto [sq_db, cq_db] = doorbells_for(0);
            _queues.emplace_back(new queue_t {
                static_cast<std::uint16_t>(_queue_depth), sq_db, cq_db
            });
        }

        _regs.store(regs::aqa,
            (static_cast<std::uint32_t>(_queue_depth - 1) << 16) | (_queue_depth - 1)
        );
        _regs.store(regs::asq, admin_queue()->sq_paddr());
        _regs.store(regs::acq, admin_queue()->cq_paddr());

        _regs.store(regs::cc,
            flags::cc::iosqes(6) | flags::cc::iocqes(4) |
            flags::cc::mps(std::countr_zero(pmm::page_size) - 12) |
            flags::cc::css(0b000) | flags::cc::ams(0b000)
        );

        if (!toggle(true))
        {
            lib::error("nvme: failed to enable controller");
            return std::unexpected { lib::err::io_error };
        }
        _enabled = true;

        const auto submit_poll = [&](const std::shared_ptr<command_t> &cmd) {
            const auto clock = chrono::main_timer();
            const auto deadline = clock->ns() + admin_timeout_ms * 1'000'000ul;

            auto &aq = admin_queue();
            aq->submit(cmd);
            while (!cmd->done())
            {
                aq->process();
                if ((_regs.load(regs::csts) & flags::csts::cfs) || clock->ns() >= deadline)
                    return false;
                arch::pause();
            }
            return true;
        };

        arch::dma_object<spec::identify_controller_t> idctrl { &_pool };
        {
            auto cmd = create_cmd();
            auto &buf = cmd->buffer().identify;

            buf.opcode = static_cast<std::uint8_t>(spec::admin_opcode::identify);
            buf.cns = spec::identify_controller;

            cmd->setup(idctrl.view_buffer());

            if (!submit_poll(cmd) || !cmd->get().first.successful())
            {
                lib::error("nvme: could not identify controller");
                return std::unexpected { lib::err::io_error };
            }
        }
        lib::info("nvme: identified controller: {:X}:{:X}", idctrl->vid, idctrl->ssvid);

        if (const auto type = idctrl->cntrltype; type != 0 && type != 1)
        {
            lib::error("nvme: unsupported controller type {}", type);
            return std::unexpected { lib::err::io_error };
        }

        _vwc = idctrl->vwc & 1;

        {
            std::size_t maxtshft = 20;
            if (idctrl->mdts != 0)
                maxtshft = 12 + (cap & flags::cap::mpsmin) + idctrl->mdts;
            _max_transfer = 1uz << maxtshft;
        }

        const auto num_cpus = cpu::count();
        auto io_queues = num_cpus;
        {
            auto cmd = create_cmd();
            auto &buf = cmd->buffer().set_features;

            buf.opcode = static_cast<std::uint8_t>(spec::admin_opcode::set_features);
            buf.data[0] = spec::number_of_queues;
            buf.data[1] = (static_cast<std::uint32_t>(io_queues - 1) << 16) | (io_queues - 1);

            command_t::result res;
            if (!submit_poll(cmd) || !(res = cmd->get()).first.successful())
            {
                lib::error("nvme: could not set number of io queues");
                return std::unexpected { lib::err::io_error };
            }

            const auto nsqa = (res.second.u32 & 0xFFFF) + 1uz;
            const auto ncqa = ((res.second.u32 >> 16) & 0xFFFF) + 1uz;
            io_queues = std::min(io_queues, std::min(nsqa, ncqa));
        }

        const auto bsp_idx = cpu::bsp_idx();
        const auto num_queues = io_queues + 1;

        const auto try_alloc = [&](auto for_device, auto alloc, pci::irq_type type) {
            auto domain = for_device(*_dev);
            if (!domain)
                return false;

            for (auto count = std::min(num_queues, domain->vec_count()); count >= 1; count /= 2)
            {
                if (auto res = alloc(*_dev, count, bsp_idx))
                {
                    _irq_handles = std::move(*res);
                    lib::bug_on(_irq_handles.size() != count);
                    _irq_type = type;
                    return true;
                }
            }
            return false;
        };

        if (try_alloc(pci::msix::for_device, pci::msix::alloc, pci::irq_type::msix) ||
            try_alloc(pci::msi::for_device, pci::msi::alloc, pci::irq_type::msi))
        {
            const auto num_irqs = _irq_handles.size();

            const auto vector_of = [&](std::size_t qid) {
                if (qid == 0 || num_irqs == 1)
                    return 0uz;
                return 1 + (qid - 1) % (num_irqs - 1);
            };

            const auto cpu_of = [&](std::size_t vector) {
                return vector == 0 ? bsp_idx : vector - 1;
            };

            for (std::size_t qid = 0; qid < num_queues; qid++)
            {
                const auto vector = vector_of(qid);

                auto it = _workers.find(vector);
                if (it == _workers.end())
                {
                    auto worker = std::make_unique<worker_t>();
                    worker->_ctrl = this;
                    worker->_cpu = cpu_of(vector);
                    worker->_mask = (_irq_type != pci::irq_type::msix) ? (1u << vector) : 0;
                    worker->_ack = false;
                    it = _workers.emplace(vector, std::move(worker)).first;
                }

                it->second->_qids.push_back(qid);
            }

            lib::bitmap affinity { num_cpus };
            for (const auto &[vector, worker] : _workers)
            {
                const auto handle = _irq_handles[vector];

                if (vector != 0)
                {
                    affinity[worker->_cpu] = true;
                    if (!irq::set_affinity(handle, affinity))
                    {
                        lib::error("nvme: failed to set irq affinity");
                        return std::unexpected { lib::err::target_is_busy };
                    }
                    affinity[worker->_cpu] = false;
                }

                if (!irq::request(handle, [ptr = worker.get()](auto) {
                    ptr->irq_handler();
                }, "nvme"))
                {
                    lib::error("nvme: failed to request irq");
                    return std::unexpected { lib::err::target_is_busy };
                }
            }
        }
        else
        {
            auto worker = std::make_unique<worker_t>();
            worker->_ctrl = this;
            worker->_cpu = bsp_idx;
            worker->_ack = false;
            worker->_qids.reserve(num_queues);
            for (std::size_t qid = 0; qid < num_queues; qid++)
                worker->_qids.push_back(qid);

            auto res = _dev->request_irq([ptr = worker.get()](auto) {
                ptr->irq_handler();
            }, bsp_idx, "nvme");

            if (!res)
            {
                lib::error("nvme: failed to allocate irq");
                return std::unexpected { lib::err::target_is_busy };
            }

            auto [handle, type] = std::move(*res);
            _irq_handles.push_back(handle);
            _irq_type = type;

            worker->_mask = _irq_type != pci::irq_type::msix;

            _workers.emplace(0uz, std::move(worker));
        }

        lib::info(
            "nvme: allocated {} irq(s) of type '{}'",
            _irq_handles.size(), magic_enum::enum_name(_irq_type)
        );

        const auto create_io_queue = [&](std::uint32_t qid, std::size_t vector) {
            auto &queue = _queues[qid];
            {
                auto cmd = create_cmd();
                auto &buf = cmd->buffer().create_cq;

                buf.opcode = static_cast<std::uint8_t>(spec::admin_opcode::create_cq);
                buf.prp1 = queue->cq_paddr();
                buf.cqid = qid;
                buf.qsize = _queue_depth - 1;
                buf.cqflags = spec::queue_phys_contig | spec::cq_irq_enabled;
                buf.irq_vector = vector;

                if (!submit_poll(cmd) || !cmd->get().first.successful())
                    return false;
            }
            {
                auto cmd = create_cmd();
                auto &buf = cmd->buffer().create_sq;

                buf.opcode = static_cast<std::uint8_t>(spec::admin_opcode::create_sq);
                buf.prp1 = queue->sq_paddr();
                buf.sqid = buf.cqid = qid;
                buf.qsize = _queue_depth - 1;
                buf.sqflags = spec::queue_phys_contig;

                if (!submit_poll(cmd) || !cmd->get().first.successful())
                    return false;
            }
            return true;
        };

        lib::info("nvme: creating {} io queues", num_queues - 1);
        _queues.resize(num_queues);
        for (const auto &[vector, worker] : _workers)
        {
            for (const auto qid : worker->_qids)
            {
                if (qid == 0)
                    continue;

                auto [sq_db, cq_db] = doorbells_for(qid);
                _queues[qid] = std::make_unique<queue_t>(_queue_depth, sq_db, cq_db);

                if (!create_io_queue(qid, vector))
                {
                    lib::error("nvme: could not create io queue");
                    return std::unexpected { lib::err::io_error };
                }
            }
        }

        {
            arch::dma_array<std::uint32_t> nslist {
                &_pool, pmm::page_size / sizeof(std::uint32_t)
            };
            {
                auto cmd = create_cmd();
                auto &buf = cmd->buffer().identify;

                buf.opcode = static_cast<std::uint8_t>(spec::admin_opcode::identify);
                buf.cns = spec::identify_active_list;
                buf.nsid = 0;
                cmd->setup(nslist.view_buffer());

                if (!submit_poll(cmd) || !cmd->get().first.successful())
                {
                    lib::error("nvme: could not list namespaces");
                    return std::unexpected { lib::err::io_error };
                }
            }

            const auto io_queues = std::span { _queues } .subspan(1);
            for (std::size_t i = 0; i < nslist.size(); i++)
            {
                const auto nsid = nslist[i];
                if (nsid == 0)
                    break;

                arch::dma_object<spec::identify_namespace_t> idns { &_pool };
                {
                    auto cmd = create_cmd();
                    auto &buf = cmd->buffer().identify;

                    buf.opcode = static_cast<std::uint8_t>(spec::admin_opcode::identify);
                    buf.cns = spec::identify_namespace;
                    buf.nsid = nsid;
                    cmd->setup(idns.view_buffer());

                    if (!submit_poll(cmd) || !cmd->get().first.successful())
                    {
                        lib::error("nvme: could not identify namespace {}", nsid);
                        return std::unexpected { lib::err::io_error };
                    }
                }

                if (idns->nsze == 0)
                    continue;

                const auto lba_shift = idns->lbaf[idns->flbas & 0x0F].ds;
                if (lba_shift < 9)
                    continue;

                auto ns = std::make_shared<namespace_t>(
                    nsid, lba_shift, idns->nsze, _pool, io_queues, _max_transfer, _vwc
                );
                lib::info("nvme: namespace {}, size: {} mib", nsid, ns->size_bytes() / 1024 / 1024);
                _namespaces.push_back(std::move(ns));
            }
        }

        for (const auto &[_, worker] : _workers)
            worker->start();

        for (const auto &handle : _irq_handles)
            irq::unmask(handle);

        if (_irq_type == pci::irq_type::intx)
            _dev->write<16>(pci::reg::cmd, _dev->read<16>(pci::reg::cmd) & ~pci::cmd::int_dis);

        return { };
    }

    controller_t::~controller_t()
    {
        if (_enabled)
        {
            _regs.store(regs::cc, _regs.load(regs::cc) / flags::cc::shn(0b01));

            const auto clock = chrono::main_timer();
            const auto end = clock->ns() + _toggle_wait_ms * 1'000'000ul;

            while (clock->ns() < end)
            {
                const auto csts = _regs.load(regs::csts);
                if ((csts & flags::csts::shst_mask) == flags::csts::shst_complete)
                    break;
                arch::pause();
            }

            toggle(false);
        }

        if (!_irq_handles.empty())
            _dev->release_irqs(_irq_handles, _irq_type);
    }

    lib::expect<std::shared_ptr<controller_t>> controller_t::create(pci::device_t &dev)
    {
        auto ctrl = std::shared_ptr<controller_t> { new controller_t { dev.dev } };
        if (const auto ret = ctrl->init(); !ret)
            return std::unexpected { ret.error() };
        return ctrl;
    }
} // namespace nvme
