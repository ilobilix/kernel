// Copyright (C) 2024-2026  ilobilo

module nvme;

import system.memory.phys;
import system.chrono;
import system.cpu;
import magic_enum;
import arch;

namespace nvme
{
    void controller_t::drain(worker_t &worker)
    {
        for (const auto &qid : worker.qids)
        {
            lib::bug_on(_queues.size() <= qid);
            _queues[qid]->process();
        }

        if (worker.mask)
            _regs.store(regs::intmc, worker.mask);
    }

    void controller_t::irq_handler(worker_t &worker)
    {
        if (worker.mask)
            _regs.store(regs::intms, worker.mask);

        if (worker.thread)
            worker.thread->wake();
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

        auto alloc = _dev->request_irqs(
            num_queues, bsp_idx,
            [this](std::size_t vector, std::size_t) {
                return [this, vector](auto) {
                    irq_handler(*_workers[vector]);
                };
            }, "nvme"
        );

        if (!alloc)
        {
            lib::error("nvme: failed to allocate irqs: {}", lib::error_name(alloc.error()));
            return std::unexpected { lib::err::target_is_busy };
        }

        _irqs = std::move(*alloc);

        const auto num_irqs = _irqs.handles.size();

        const auto vector_of = [&](std::size_t qid) {
            if (qid == 0 || num_irqs == 1)
                return 0uz;
            return 1 + (qid - 1) % (num_irqs - 1);
        };

        const auto cpu_of = [&](std::size_t vector) {
            return vector == 0 ? bsp_idx : vector - 1;
        };

        _workers.clear();
        _workers.reserve(num_irqs);
        for (std::size_t vector = 0; vector < num_irqs; vector++)
            _workers.push_back(std::make_unique<worker_t>());

        for (std::size_t qid = 0; qid < num_queues; qid++)
            _workers[vector_of(qid)]->qids.push_back(qid);

        lib::bitmap affinity { num_cpus };
        for (std::size_t vector = 0; vector < num_irqs; vector++)
        {
            auto &worker = *_workers[vector];
            const auto cpu = cpu_of(vector);

            worker.mask = (_irqs.type != pci::irq_type::msix) ? (1u << vector) : 0;

            if (vector != 0 && _irqs.type != pci::irq_type::intx)
            {
                affinity[cpu] = true;
                if (!irq::set_affinity(_irqs.handles[vector], affinity))
                    lib::warn("nvme: failed to set irq {} affinity to cpu {}", vector, cpu);
                affinity[cpu] = false;
            }

            worker.thread = std::make_unique<sched::irq_worker_t>(
                "nvme", cpu, [this, ptr = &worker] { drain(*ptr); }
            );
        }

        lib::info(
            "nvme: allocated {} irq(s) of type '{}'",
            _irqs.handles.size(), magic_enum::enum_name(_irqs.type)
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
        for (const auto &[vector, worker] : _workers | std::views::enumerate)
        {
            for (const auto qid : worker->qids)
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
                    nsid, lba_shift, idns->nsze, _pool,
                    io_queues, _max_transfer >> lba_shift, _vwc
                );
                lib::info("nvme: namespace {}, size: {} mib", nsid, ns->size_bytes() / 1024 / 1024);
                _namespaces.push_back(std::move(ns));
            }
        }

        for (const auto &worker : _workers)
        {
            if (const auto ret = worker->thread->start(); !ret)
            {
                lib::error("nvme: failed to start irq worker: {}", lib::error_name(ret.error()));
                return std::unexpected { ret.error() };
            }
        }

        for (const auto &handle : _irqs.handles)
            irq::unmask(handle);

        if (_irqs.type == pci::irq_type::intx)
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

        _irqs.release(*_dev);
        _workers.clear();
    }

    lib::expect<std::shared_ptr<controller_t>> controller_t::create(
        const std::shared_ptr<pci::device> &dev
    )
    {
        auto ctrl = std::shared_ptr<controller_t> { new controller_t { dev } };
        if (const auto ret = ctrl->init(); !ret)
            return std::unexpected { ret.error() };
        return ctrl;
    }
} // namespace nvme
