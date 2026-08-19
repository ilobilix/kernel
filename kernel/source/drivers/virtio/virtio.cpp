// Copyright (C) 2024-2026  ilobilo

module drivers.virtio;

import system.cpu;

namespace virtio
{
    namespace
    {
        struct ktype_t : dev::ktype_t
        {
            struct attribute_t : dev::make_attribute_t
            {
                using rfn_t = lib::expect<std::string> (*)(device_t &);
                using wfn_t = lib::expect<void> (*)(device_t &, std::string_view);

                attribute_t(rfn_t rfn, wfn_t wfn, std::string_view name, mode_t mode)
                    : dev::make_attribute_t {
                        [rfn](dev::device_t &dev) -> lib::expect<std::string> {
                            if (dev.bus != get_bus())
                                return std::unexpected { lib::err::io_error };
                            return rfn(static_cast<device_t &>(dev));
                        },
                        wfn == nullptr ? dev::make_attribute_t::wfn_t { } :
                        [wfn](dev::device_t &dev, std::string_view value) -> lib::expect<void> {
                            if (dev.bus != get_bus())
                                return std::unexpected { lib::err::io_error };
                            return wfn(static_cast<device_t &>(dev), value);
                        }, name, mode
                    } { }
            };

            std::span<dev::attribute_t *const> attributes() const override
            {
                static attribute_t vendor {
                    [](device_t &dev) -> lib::expect<std::string> {
                        return fmt::format("0x{:08x}\n", dev.ident.vendor);
                    }, nullptr, "vendor", 0444
                };
                static attribute_t device {
                    [](device_t &dev) -> lib::expect<std::string> {
                        return fmt::format("0x{:08x}\n", dev.ident.device);
                    }, nullptr, "device", 0444
                };
                static attribute_t modalias {
                    [](device_t &dev) -> lib::expect<std::string> {
                        return dev.modalias + "\n";
                    }, nullptr, "modalias", 0444
                };
                static attribute_t features {
                    [](device_t &dev) -> lib::expect<std::string> {
                        return fmt::format("0x{:016x}\n", dev.features());
                    }, nullptr, "features", 0444
                };
                static attribute_t status {
                    [](device_t &dev) -> lib::expect<std::string> {
                        return fmt::format("0x{:08x}\n", dev.transport().status());
                    }, nullptr, "status", 0444
                };

                static dev::attribute_t *list[] {
                    &vendor,
                    &device,
                    &modalias,
                    &features,
                    &status
                };
                return list;
            }
        };

        struct driver_ktype_t : dev::ktype_t
        {
            std::span<dev::attribute_t *const> attributes() const override
            {
                static dev::attribute_t *list[] {
                    dev::bind_attribute(),
                    dev::unbind_attribute()
                };
                return list;
            }
        };

    } // namespace

    dev::bus_t *get_bus()
    {
        static bus_t bus { };
        return &bus;
    }

    dev::ktype_t &get_ktype()
    {
        static ktype_t ktype { };
        return ktype;
    }

    dev::ktype_t &get_driver_ktype()
    {
        static driver_ktype_t ktype { };
        return ktype;
    }

    lib::expect<void> device_t::setup_irqs(std::size_t cpu)
    {
        const auto nqueues = _transport->num_queues();

        auto layout = _transport->setup_irqs(
            nqueues, cpu, [this](std::size_t vector, bool config) {
                if (vector >= _workers.size())
                    return;

                if (config)
                    _config_pending.store(true, std::memory_order_release);

                _workers[vector].thread->wake();
            }
        );
        if (!layout)
            return std::unexpected { layout.error() };

        lib::bug_on(layout->count == 0);
        lib::bug_on(layout->queues.size() != nqueues);
        lib::bug_on(layout->cpus.size() != layout->count);
        lib::bug_on(layout->config >= layout->count);

        _layout = std::move(*layout);
        _workers.reserve(_layout.count);
        _queues.resize(nqueues);

        for (std::size_t vector = 0; vector < _layout.count; vector++)
        {
            std::vector<std::uint16_t> qids;
            for (const auto &[qid, owner] : _layout.queues | std::views::enumerate)
            {
                if (owner == vector)
                    qids.push_back(qid);
            }

            auto &worker = _workers.emplace_back(
                std::move(qids), std::make_unique<sched::irq_worker_t>(
                    name, _layout.cpus[vector], [this, vector] { drain(vector); }
                )
            );

            if (const auto ret = worker.thread->start(); !ret)
            {
                lib::error("virtio: {}: could not start irq {} worker", name, vector);
                return ret;
            }
        }
        return { };
    }

    void device_t::drain(std::size_t vector)
    {
        if (vector == _layout.config && _config_pending.exchange(false, std::memory_order_acquire))
        {
            if (_config_changed)
                _config_changed();
        }

        for (const auto qid : _workers[vector].qids)
        {
            if (_queues[qid])
                _queues[qid]->reap();
        }
    }

    lib::expect<std::uint16_t> device_t::prepare_queue(std::uint16_t qid, std::uint16_t size)
    {
        if (qid >= _queues.size())
            return std::unexpected { lib::err::no_such_device };

        if (_queues[qid])
            return std::unexpected { lib::err::already_exists };

        const auto max = _transport->queue_max_size(qid);
        if (max == 0)
            return std::unexpected { lib::err::no_such_device };

        if (!std::has_single_bit(max))
        {
            lib::error("virtio: {}: queue {} has invalid size {}", name, qid, max);
            return std::unexpected { lib::err::io_error };
        }

        return (size == 0 || _transport->legacy_layout()) ? max : std::min(size, max);
    }

    lib::expect<queue_t *> device_t::install_queue(std::uint16_t qid, std::unique_ptr<queue_t> queue)
    {
        const auto vector = _layout.queues.empty()
            ? no_vector
            : _layout.queues[qid];

        if (auto ret = _transport->enable_queue(qid, queue->addr(vector)); !ret)
            return std::unexpected { ret.error() };

        _queues[qid] = std::move(queue);
        return _queues[qid].get();
    }

    lib::expect<queue_t *> device_t::setup_queue(
        std::uint16_t qid, used_fn on_used, std::uint16_t size
    )
    {
        const auto want = prepare_queue(qid, size);
        if (!want)
            return std::unexpected { want.error() };

        auto queue = queue_t::create(*_transport, qid, *want, std::move(on_used));
        if (!queue)
            return std::unexpected { queue.error() };

        return install_queue(qid, std::move(*queue));
    }

    lib::expect<queue_t *> device_t::setup_rx_queue(
        std::uint16_t qid, std::size_t nbufs, std::size_t bufsize,
        receive_fn on_receive, std::uint16_t size
    )
    {
        const auto want = prepare_queue(qid, size);
        if (!want)
            return std::unexpected { want.error() };

        const auto count = nbufs == 0 ? *want : std::min<std::size_t>(nbufs, *want);

        auto queue = queue_t::create_buf(
            *_transport, qid, *want, count, bufsize, std::move(on_receive)
        );
        if (!queue)
            return std::unexpected { queue.error() };

        return install_queue(qid, std::move(*queue));
    }

    lib::expect<std::vector<queue_t *>> device_t::setup_queues(std::span<const used_fn> fns)
    {
        std::vector<queue_t *> queues;
        queues.reserve(fns.size());

        for (const auto &[qid, fn] : fns | std::views::enumerate)
        {
            auto queue = setup_queue(qid, fn);
            if (!queue)
            {
                for (std::size_t i = 0; i < queues.size(); i++)
                {
                    _transport->disable_queue(i);
                    _queues[i].reset();
                }
                return std::unexpected { queue.error() };
            }
            queues.push_back(*queue);
        }

        return queues;
    }

    queue_t &device_t::queue(std::uint16_t qid)
    {
        lib::bug_on(qid >= _queues.size() || !_queues[qid]);
        return *_queues[qid];
    }

    void device_t::set_ready()
    {
        if (_ready.exchange(true, std::memory_order_acq_rel))
            return;

        _transport->enable_irqs();
        _transport->add_status(status::driver_ok);

        for (const auto &queue : _queues)
        {
            if (queue && queue->buffered())
                queue->submit();
        }
    }

    void device_t::freeze()
    {
        _transport->reset();
        _transport->release_irqs();
        _workers.clear();

        _config_pending.store(false, std::memory_order_relaxed);
        _ready.store(false, std::memory_order_release);
    }

    void device_t::destroy()
    {
        freeze();

        _queues.clear();
        _layout = { };
        _features = 0;
    }

    lib::expect<void> bus_t::probe(dev::device_t &_dev, dev::driver_t &_drv)
    {
        auto &dev = static_cast<device_t &>(_dev);
        auto &drv = static_cast<driver_t &>(_drv);
        auto &tp = dev.transport();

        const auto fail = [&](lib::err err) {
            tp.add_status(status::failed);

            dev.destroy();

            tp.add_status(status::acknowledge);
            tp.add_status(status::driver);

            return std::unexpected { err };
        };

        const auto offered = tp.device_features();
        const auto mandatory = tp.mandatory_features();
        auto wanted = drv.features() | mandatory;

        // TODO: iommu
        wanted &= ~feature_bit(feature::access_platform);

        dev._features = offered & wanted;
        if ((dev._features & mandatory) != mandatory)
            return fail(lib::err::not_supported);

        tp.driver_features(dev._features);
        tp.add_status(status::features_ok);

        if (!(tp.status() & status::features_ok))
            return fail(lib::err::not_supported);

        if (auto ret = dev.setup_irqs(cpu::bsp_idx()); !ret)
            return fail(ret.error());

        if (auto ret = drv.probe(dev); !ret)
            return fail(ret.error());

        dev.set_ready();
        return { };
    }

    bool bus_t::remove(dev::device_t &_dev, dev::driver_t &drv)
    {
        auto &dev = static_cast<device_t &>(_dev);
        dev.freeze();
        const bool ret = static_cast<driver_t &>(drv).remove(dev);
        dev.destroy();
        return ret;
    }

    void bus_t::fill_uevent(dev::device_t &_dev, dev::uevent_t &uev)
    {
        auto &dev = static_cast<device_t &>(_dev);

        uev.add("VIRTIO_DEVICE_ID", fmt::format("{:08x}", dev.ident.device));
        uev.add("VIRTIO_VENDOR_ID", fmt::format("{:08x}", dev.ident.vendor));
    }

    lib::initgraph::stage *bus_registered_stage()
    {
        static lib::initgraph::stage stage
        {
            "virtio.bus.registered",
            lib::initgraph::postsched_init_engine
        };
        return &stage;
    }

    namespace
    {
        lib::initgraph::task register_bus_task
        {
            "virtio.bus.register",
            lib::initgraph::postsched_init_engine,
            lib::initgraph::require { dev::core_registered_stage() },
            lib::initgraph::entail { bus_registered_stage() },
            [] {
                lib::bug_on(!dev::register_bus(*get_bus()));
            }
        };
    } // namespace
} // namespace virtio
