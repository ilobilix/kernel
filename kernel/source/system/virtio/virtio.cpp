// Copyright (C) 2024-2026  ilobilo

module system.virtio;

import system.pci;

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

    // TODO
    // lib::expect<queue_t *> device_t::setup_queue(
    //     std::uint16_t qid, used_fn on_used, std::uint16_t size
    // );
    // lib::expect<std::vector<queue_t *>> device_t::setup_queues(std::span<const used_fn> fns);
    // queue_t &device_t::queue(std::uint16_t qid);

    void device_t::set_ready()
    {
        if (_ready.exchange(true, std::memory_order_acq_rel))
            return;

        _transport->add_status(status::driver_ok);
    }

    void device_t::destroy()
    {
        _transport->reset();
        _transport->release_irqs();

        _queues.clear();

        _ready.store(false, std::memory_order_release);
        _features = 0;
    }

    lib::expect<void> bus_t::probe(dev::device_t &_dev, dev::driver_t &_drv)
    {
        auto &dev = static_cast<device_t &>(_dev);
        auto &drv = static_cast<driver_t &>(_drv);
        auto &tp = dev.transport();

        const auto fail = [&](lib::err err) {
            tp.add_status(status::failed);

            tp.reset();
            tp.add_status(status::acknowledge);
            tp.add_status(status::driver);

            dev._queues.clear();
            dev._features = 0;

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

        if (auto ret = drv.probe(dev); !ret)
            return fail(ret.error());

        dev.set_ready();
        return { };
    }

    bool bus_t::remove(dev::device_t &_dev, dev::driver_t &drv)
    {
        auto &dev = static_cast<device_t &>(_dev);
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
