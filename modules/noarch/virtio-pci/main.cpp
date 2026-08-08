// Copyright (C) 2024-2026  ilobilo

import system.virtio;
import system.chrono;
import system.dev;
import system.pci;
import system.irq;
import system.cpu;
import magic_enum;
import libarch;
import arch;
import fmt;
import lib;
import std;

namespace virtio::pci
{
    using namespace ::pci;

    namespace
    {
        constexpr std::size_t reset_timeout_ms = 100;
        constexpr std::uint8_t cap_id = 0x09;

        namespace ids
        {
            constexpr std::uint16_t vendor = 0x1AF4;

            constexpr std::uint16_t modern_base = 0x1040;
            constexpr std::uint16_t modern_last = 0x107F;
            constexpr std::uint16_t legacy_base = 0x1000;
            constexpr std::uint16_t legacy_last = 0x103F;
        } // namespace ids

        enum cfg_type : std::uint8_t
        {
            common_cfg = 1,
            notify_cfg = 2,
            isr_cfg = 3,
            device_cfg = 4,
            pci_cfg = 5,
            shared_memory_cfg = 8,
            vendor_cfg = 9,
            cfg_type_max = 10
        };

        namespace cap
        {
            // virtio_pci_cap
            constexpr std::size_t cap_len = 0x02;
            constexpr std::size_t cfg_type = 0x03;
            constexpr std::size_t bar = 0x04;
            constexpr std::size_t offset = 0x08;
            constexpr std::size_t length = 0x0C;

            // virtio_pci_notify_cap
            constexpr std::size_t notify_off_multiplier = 0x10;

            constexpr std::uint8_t min_len = 0x10;
            constexpr std::uint8_t notify_len = 0x14;
        } // namespace cap

        // virtio_pci_common_cfg
        namespace regs
        {
            constexpr arch::scalar_register<std::uint32_t> device_feature_select { 0x00 };
            constexpr arch::scalar_register<std::uint32_t> device_feature { 0x04 };
            constexpr arch::scalar_register<std::uint32_t> driver_feature_select { 0x08 };
            constexpr arch::scalar_register<std::uint32_t> driver_feature { 0x0C };
            constexpr arch::scalar_register<std::uint16_t> config_msix_vector { 0x10 };
            constexpr arch::scalar_register<std::uint16_t> num_queues { 0x12 };
            constexpr arch::scalar_register<std::uint8_t> device_status { 0x14 };
            constexpr arch::scalar_register<std::uint8_t> config_generation { 0x15 };

            constexpr arch::scalar_register<std::uint16_t> queue_select { 0x16 };
            constexpr arch::scalar_register<std::uint16_t> queue_size { 0x18 };
            constexpr arch::scalar_register<std::uint16_t> queue_msix_vector { 0x1A };
            constexpr arch::scalar_register<std::uint16_t> queue_enable { 0x1C };
            constexpr arch::scalar_register<std::uint16_t> queue_notify_off { 0x1E };
            constexpr arch::scalar_register<std::uint64_t> queue_desc { 0x20 };
            constexpr arch::scalar_register<std::uint64_t> queue_driver { 0x28 };
            constexpr arch::scalar_register<std::uint64_t> queue_device { 0x30 };
            // constexpr arch::scalar_register<std::uint16_t> queue_notif_config_data { 0x38 };
            // constexpr arch::scalar_register<std::uint16_t> queue_reset { 0x3A };

            // constexpr arch::scalar_register<std::uint16_t> admin_queue_index { 0x3C };
            // constexpr arch::scalar_register<std::uint16_t> admin_queue_num { 0x3E };

            constexpr std::size_t common_cfg_size = 0x38;
        } // namespace regs

        namespace isr
        {
            constexpr std::uint8_t queue = 1;
            constexpr std::uint8_t config = 2;
        } // namespace isr

        struct cap_info_t
        {
            std::uint8_t bar;
            std::uint32_t offset;
            std::uint32_t length;
        };

        struct region_t
        {
            arch::mem_space space { };
            std::uint32_t length = 0;

            explicit operator bool() const { return length != 0; }
        };

        std::uint16_t table_entry(
            device &dev, const irq_alloc_t &irqs, std::uint16_t vector
        )
        {
            if (irqs.type != irq_type::msix || vector >= irqs.handles.size())
                return no_vector;
            return msix::vector_of(dev, irqs.handles[vector]).value_or(no_vector);
        }

        lib::expect<void> write_vector(
            auto &space, const arch::scalar_register<std::uint16_t> &reg, bool present,
            device &dev, const irq_alloc_t &irqs, std::uint16_t vector
        )
        {
            if (!present)
                return { };

            const auto entry = table_entry(dev, irqs, vector);
            space.store(reg, entry);

            if (space.load(reg) != entry)
            {
                lib::error("virtio-pci: device refused msi-x vector {}", entry);
                return std::unexpected { lib::err::io_error };
            }
            return { };
        }

        void enable_irqs(device &dev, const irq_alloc_t &irqs)
        {
            for (const auto &handle : irqs.handles)
                irq::unmask(handle);

            if (irqs.type == irq_type::intx)
                dev.write<16>(reg::cmd, dev.read<16>(reg::cmd) & ~cmd::int_dis);
        }

        lib::expect<irq_layout_t> setup_irqs(
            device &dev, virtio::transport_t &tp, irq_alloc_t &irqs,
            std::atomic_bool &shared, std::uint16_t nqueues, std::size_t cpu,
            vector_fn on_trigger, auto &&write_config
        )
        {
            const auto desired = msix::for_device(dev)
                ? std::min<std::size_t>(nqueues, cpu::count()) + 1uz
                : 1uz;

            auto alloc = dev.request_irqs(
                desired, cpu, [&](std::size_t vector, std::size_t count) {
                    return [&tp, &shared, on_trigger, self = vector, config = count - 1](auto) {
                        if (!shared.load(std::memory_order_relaxed))
                        {
                            on_trigger(self, self == config);
                            return;
                        }

                        const auto status = tp.isr_status();
                        if (!(status & (isr::queue | isr::config)))
                            return;

                        on_trigger(0, status & isr::config);
                    };
                }, "virtio-pci"
            );

            if (!alloc)
                return std::unexpected { alloc.error() };

            const bool msix = alloc->type == irq_type::msix;
            shared.store(!msix, std::memory_order_relaxed);

            const auto count = alloc->handles.size();

            const auto usable = msix ? count : 1;
            const auto qvecs = usable > 1 ? usable - 1 : 1;

            irq_layout_t layout;
            layout.queues.resize(nqueues);
            layout.cpus.resize(count);
            std::ranges::fill(layout.cpus, cpu);
            layout.config = usable - 1;
            layout.count = count;

            for (std::uint16_t qid = 0; qid < nqueues; qid++)
                layout.queues[qid] = qid % qvecs;

            irqs = std::move(*alloc);

            if (auto ret = write_config(layout.config); !ret)
            {
                irqs.release(dev);
                return std::unexpected { ret.error() };
            }

            if (msix && usable > 1)
            {
                const auto ncpus = ::cpu::count();
                lib::bitmap affinity { ncpus };

                for (std::size_t vector = 0; vector < qvecs; vector++)
                {
                    const auto target = (cpu + vector) % ncpus;
                    if (target == cpu)
                        continue;

                    affinity[target] = true;
                    if (!irq::set_affinity(irqs.handles[vector], affinity))
                    {
                        lib::warn(
                            "virtio-pci: could not set vector {} affinity to cpu {}",
                            vector, target
                        );
                    }
                    else layout.cpus[vector] = target;
                    affinity[target] = false;
                }
            }

            const auto num = irqs.handles.size();
            lib::info(
                "virtio-pci: allocated {} vector{} of type '{}'",
                num, num != 1 ? "s" : "", magic_enum::enum_name(irqs.type)
            );
            return layout;
        }

        void do_reset(auto &&load_status, auto &&store_status)
        {
            store_status(0);

            const auto clock = chrono::main_timer();
            const auto end = clock->ns() + (reset_timeout_ms * 1'000'000ul);

            while (load_status() != 0)
            {
                if (clock->ns() > end)
                {
                    lib::error("virtio-pci: device did not acknowledge reset");
                    return;
                }
                arch::pause();
            }
        }

        lib::expect<region_t> map_region(
            const std::shared_ptr<device> &dev, const cap_info_t &info
        )
        {
            const auto bars = dev->get_bars();
            if (info.bar >= bars.size())
            {
                lib::error("virtio-pci: capability points to invalid bar {}", info.bar);
                return std::unexpected { lib::err::invalid_argument };
            }

            auto &bar = bars[info.bar];
            if (bar.type != bar::type::mem)
            {
                lib::error("virtio-pci: bar {} type is not mmio", info.bar);
                return std::unexpected { lib::err::not_supported };
            }

            const auto end = static_cast<std::uint64_t>(info.offset) + info.length;
            if (info.length == 0 || end > bar.size)
            {
                lib::error(
                    "virtio-pci: capability at 0x{:X}-0x{:X} is outside bar {} with size 0x{:X}",
                    info.offset, info.offset + info.length, info.bar, bar.size
                );
                return std::unexpected { lib::err::addr_out_of_bounds };
            }

            return region_t {
                arch::mem_space { bar.map() + info.offset },
                info.length
            };
        }

        class pci_transport_t final : public transport_t
        {
            private:
            std::shared_ptr<device> _dev;

            region_t _common;
            region_t _notify;
            region_t _isr;
            region_t _device_cfg;

            irq_alloc_t _irqs;
            std::atomic_bool _shared_isr;

            std::uint32_t _notify_mult;
            std::vector<std::size_t> _notify_offsets;

            lib::spinlock _lock;

            void select(std::uint16_t qid)
            {
                _common.space.store(regs::queue_select, qid);
            }

            lib::expect<void> write_vector(
                const arch::scalar_register<std::uint16_t> &reg, std::uint16_t vector
            )
            {
                return pci::write_vector(_common.space, reg, true, *_dev, _irqs, vector);
            }

            public:
            pci_transport_t(
                const std::shared_ptr<device> &dev,
                region_t common, region_t notify, region_t isr, region_t device_cfg,
                std::uint32_t notify_multiplier
            ) : _dev { dev }, _common { common }, _notify { notify }, _isr { isr },
                _device_cfg { device_cfg }, _irqs { }, _shared_isr { true },
                _notify_mult { notify_multiplier }, _notify_offsets { }, _lock { }
            {
                _notify_offsets.resize(_common.space.load(regs::num_queues), 0);
            }

            std::string_view type() const override { return "pci-modern"; }

            std::uint64_t device_features() override
            {
                const std::unique_lock _ { _lock };

                _common.space.store(regs::device_feature_select, 0);
                const auto low = _common.space.load(regs::device_feature);

                _common.space.store(regs::device_feature_select, 1);
                const std::uint64_t high = _common.space.load(regs::device_feature);

                return (high << 32) | low;
            }

            void driver_features(std::uint64_t feat) override
            {
                const std::unique_lock _ { _lock };

                _common.space.store(regs::driver_feature_select, 0);
                _common.space.store(regs::driver_feature, static_cast<std::uint32_t>(feat));

                _common.space.store(regs::driver_feature_select, 1);
                _common.space.store(regs::driver_feature, static_cast<std::uint32_t>(feat >> 32));
            }

            std::uint64_t mandatory_features() const override
            {
                return feature_bit(feature::version_1);
            }

            std::uint8_t status() override
            {
                return _common.space.load(regs::device_status);
            }

            void add_status(std::uint8_t bits) override
            {
                const std::unique_lock _ { _lock };
                const auto current = _common.space.load(regs::device_status);
                _common.space.store(regs::device_status, current | bits);
            }

            void reset() override
            {
                const std::unique_lock _ { _lock };
                do_reset(
                    [this] { return _common.space.load(regs::device_status); },
                    [this](std::uint8_t val) { _common.space.store(regs::device_status, val); }
                );
            }

            ~pci_transport_t() override
            {
                reset();
                _irqs.release(*_dev);
            }

            std::uint16_t num_queues() override
            {
                return _common.space.load(regs::num_queues);
            }

            std::uint16_t queue_max_size(std::uint16_t qid) override
            {
                if (qid >= _notify_offsets.size())
                    return 0;

                const std::unique_lock _ { _lock };
                select(qid);
                return _common.space.load(regs::queue_size);
            }

            lib::expect<void> enable_queue(std::uint16_t qid, const queue_addr_t &addr) override
            {
                if (qid >= _notify_offsets.size())
                    return std::unexpected { lib::err::no_such_device };

                if (addr.size == 0 || !std::has_single_bit(addr.size))
                    return std::unexpected { lib::err::invalid_argument };

                const std::unique_lock _ { _lock };
                select(qid);

                const auto max = _common.space.load(regs::queue_size);
                if (max == 0)
                    return std::unexpected { lib::err::no_such_device };

                if (addr.size > max)
                    return std::unexpected { lib::err::invalid_argument };

                _common.space.store(regs::queue_size, addr.size);
                _common.space.store(regs::queue_desc, addr.desc);
                _common.space.store(regs::queue_driver, addr.avail);
                _common.space.store(regs::queue_device, addr.used);

                if (auto ret = write_vector(regs::queue_msix_vector, addr.vector); !ret)
                    return ret;

                const std::size_t notify_off = _common.space.load(regs::queue_notify_off);
                const auto offset = notify_off * _notify_mult;

                if (offset + sizeof(std::uint16_t) > _notify.length)
                {
                    lib::error(
                        "virtio-pci: queue {} notify offset 0x{:X} "
                        "is outside the notify region (0x{:X} bytes)",
                        qid, offset, _notify.length
                    );
                    return std::unexpected { lib::err::addr_out_of_bounds };
                }

                _notify_offsets[qid] = offset;
                _common.space.store(regs::queue_enable, 1);
                return { };
            }

            void disable_queue(std::uint16_t qid) override
            {
                if (qid >= _notify_offsets.size())
                    return;

                const std::unique_lock _ { _lock };
                select(qid);

                _common.space.store(regs::queue_enable, 0);
                _common.space.store(regs::queue_msix_vector, no_vector);
            }

            void notify(std::uint16_t qid) override
            {
                lib::bug_on(qid >= _notify_offsets.size());

                _notify.space.store(
                    arch::scalar_register<std::uint16_t> {
                        static_cast<std::ptrdiff_t>(_notify_offsets[qid])
                    }, qid
                );
            }

            void read_config(std::size_t off, std::span<std::byte> buffer) override
            {
                lib::bug_on(off + buffer.size() > _device_cfg.length);

                for (std::size_t i = 0; i < buffer.size(); i++)
                {
                    buffer[i] = static_cast<std::byte>(_device_cfg.space.load(
                        arch::scalar_register<std::uint8_t> {
                            static_cast<std::ptrdiff_t>(off + i)
                        }
                    ));
                }
            }

            void write_config(std::size_t off, std::span<const std::byte> buffer) override
            {
                lib::bug_on(off + buffer.size() > _device_cfg.length);

                for (std::size_t i = 0; i < buffer.size(); i++)
                {
                    _device_cfg.space.store(
                        arch::scalar_register<std::uint8_t> {
                            static_cast<std::ptrdiff_t>(off + i)
                        },
                        static_cast<std::uint8_t>(buffer[i])
                    );
                }
            }

            std::uint8_t config_generation() override
            {
                return _common.space.load(regs::config_generation);
            }

            lib::expect<irq_layout_t> setup_irqs(
                std::uint16_t nqueues, std::size_t cpu, vector_fn on_trigger
            ) override
            {
                return pci::setup_irqs(
                    *_dev, *this, _irqs, _shared_isr, nqueues, cpu, std::move(on_trigger),
                    [this](std::uint16_t vector) {
                        const std::unique_lock _ { _lock };
                        return write_vector(regs::config_msix_vector, vector);
                    }
                );
            }

            void enable_irqs() override
            {
                pci::enable_irqs(*_dev, _irqs);
            }

            void release_irqs() override
            {
                _irqs.release(*_dev);
            }

            std::uint8_t isr_status() override
            {
                if (!_isr)
                    return 0;
                return _isr.space.load(arch::scalar_register<std::uint8_t> { 0 });
            }
        };

        namespace legacy
        {
            namespace regs
            {
                constexpr arch::scalar_register<std::uint32_t> device_features { 0x00 };
                constexpr arch::scalar_register<std::uint32_t> driver_features { 0x04 };
                constexpr arch::scalar_register<std::uint32_t> queue_pfn { 0x08 };
                constexpr arch::scalar_register<std::uint16_t> queue_size { 0x0C };
                constexpr arch::scalar_register<std::uint16_t> queue_select { 0x0E };
                constexpr arch::scalar_register<std::uint16_t> queue_notify { 0x10 };
                constexpr arch::scalar_register<std::uint8_t> device_status { 0x12 };
                constexpr arch::scalar_register<std::uint8_t> isr_status { 0x13 };

                constexpr arch::scalar_register<std::uint16_t> config_msix_vector { 0x14 };
                constexpr arch::scalar_register<std::uint16_t> queue_msix_vector { 0x16 };

                constexpr std::size_t config = 0x14;
                constexpr std::size_t config_msix = 0x18;
            } // namespace regs

            constexpr std::uint16_t max_queues = 64;

            class transport_t final : public virtio::transport_t
            {
                private:
                std::shared_ptr<device> _dev;
                arch::io_space _io;
                std::size_t _io_size;

                irq_alloc_t _irqs;
                std::atomic_bool _shared_isr;
                std::uint16_t _num_queues;

                lib::spinlock _lock;

                lib::expect<void> write_vector(
                    const arch::scalar_register<std::uint16_t> &reg, std::uint16_t vector
                )
                {
                    return pci::write_vector(
                        _io, reg, msix::is_enabled(*_dev), *_dev, _irqs, vector
                    );
                }

                std::size_t config_offset() const
                {
                    return msix::is_enabled(*_dev)
                        ? regs::config_msix
                        : regs::config;
                }

                bool config_fits(std::size_t off, std::size_t len) const
                {
                    return config_offset() + off + len <= _io_size;
                }

                void select(std::uint16_t qid)
                {
                    _io.store(regs::queue_select, qid);
                }

                public:
                transport_t(
                    const std::shared_ptr<device> &dev,
                    arch::io_space io, std::size_t io_size
                ) : _dev { dev }, _io { io }, _io_size { io_size }, _irqs { }, _shared_isr { true },
                    _num_queues { 0 }, _lock { }
                {
                    reset();

                    for (std::uint16_t qid = 0; qid < max_queues; qid++)
                    {
                        select(qid);
                        if (_io.load(regs::queue_size) == 0)
                            break;
                        _num_queues = qid + 1;
                    }
                }

                std::string_view type() const override { return "pci-legacy"; }

                std::uint64_t device_features() override
                {
                    return _io.load(regs::device_features);
                }

                void driver_features(std::uint64_t feat) override
                {
                    lib::bug_on((feat >> 32) != 0);
                    _io.store(regs::driver_features, static_cast<std::uint32_t>(feat));
                }

                std::uint64_t mandatory_features() const override { return 0; }

                std::uint8_t status() override
                {
                    return _io.load(regs::device_status);
                }

                void add_status(std::uint8_t bits) override
                {
                    const std::unique_lock _ { _lock };
                    const auto current = _io.load(regs::device_status);
                    _io.store(regs::device_status, current | bits);
                }

                void reset() override
                {
                    const std::unique_lock _ { _lock };
                    do_reset(
                        [this] { return _io.load(regs::device_status); },
                        [this](std::uint8_t val) { _io.store(regs::device_status, val); }
                    );
                }

                ~transport_t() override
                {
                    reset();
                    _irqs.release(*_dev);
                }

                std::uint16_t num_queues() override { return _num_queues; }
                bool legacy_layout() const override { return true; }

                std::uint16_t queue_max_size(std::uint16_t qid) override
                {
                    if (qid >= _num_queues)
                        return 0;

                    const std::unique_lock _ { _lock };
                    select(qid);
                    return _io.load(regs::queue_size);
                }

                lib::expect<void> enable_queue(std::uint16_t qid, const queue_addr_t &addr) override
                {
                    if (qid >= _num_queues)
                        return std::unexpected { lib::err::no_such_device };

                    if (addr.size == 0 || !std::has_single_bit(addr.size))
                        return std::unexpected { lib::err::invalid_argument };

                    const std::unique_lock _ { _lock };
                    select(qid);

                    const auto max = _io.load(regs::queue_size);
                    if (max == 0)
                        return std::unexpected { lib::err::no_such_device };

                    if (addr.size != max)
                    {
                        lib::error(
                            "virtio-pci: queue {} size is {} (requested {})",
                            qid, max, addr.size
                        );
                        return std::unexpected { lib::err::invalid_argument };
                    }

                    const auto layout = get_layout(addr.size, true);
                    if ((addr.desc % vring_align) != 0 ||
                        addr.avail != addr.desc + layout.avail_off ||
                        addr.used != addr.desc + layout.used_off)
                    {
                        lib::error("virtio-pci: queue {} rings are not in legacy layout", qid);
                        return std::unexpected { lib::err::invalid_argument };
                    }

                    const auto pfn = addr.desc / vring_align;
                    if (pfn > std::numeric_limits<std::uint32_t>::max())
                    {
                        lib::error("virtio-pci: queue {} is out of bounds of the pfn", qid);
                        return std::unexpected { lib::err::addr_out_of_bounds };
                    }

                    if (auto ret = write_vector(regs::queue_msix_vector, addr.vector); !ret)
                        return ret;

                    _io.store(regs::queue_pfn, pfn);
                    return { };
                }

                void disable_queue(std::uint16_t qid) override
                {
                    if (qid >= _num_queues)
                        return;

                    const std::unique_lock _ { _lock };
                    select(qid);
                    _io.store(regs::queue_pfn, 0);
                }

                void notify(std::uint16_t qid) override
                {
                    lib::bug_on(qid >= _num_queues);
                    _io.store(regs::queue_notify, qid);
                }

                void read_config(std::size_t off, std::span<std::byte> buffer) override
                {
                    lib::bug_on(!config_fits(off, buffer.size()));

                    const auto base = config_offset();
                    for (std::size_t i = 0; i < buffer.size(); i++)
                    {
                        buffer[i] = static_cast<std::byte>(_io.load(
                            arch::scalar_register<std::uint8_t> {
                                static_cast<std::ptrdiff_t>(base + off + i)
                            }
                        ));
                    }
                }

                void write_config(std::size_t off, std::span<const std::byte> buffer) override
                {
                    lib::bug_on(!config_fits(off, buffer.size()));

                    const auto base = config_offset();
                    for (std::size_t i = 0; i < buffer.size(); i++)
                    {
                        _io.store(
                            arch::scalar_register<std::uint8_t> {
                                static_cast<std::ptrdiff_t>(base + off + i)
                            },
                            static_cast<std::uint8_t>(buffer[i])
                        );
                    }
                }

                std::uint8_t config_generation() override { return 0; }

                lib::expect<irq_layout_t> setup_irqs(
                    std::uint16_t nqueues, std::size_t cpu, vector_fn on_trigger
                ) override
                {
                    return pci::setup_irqs(
                        *_dev, *this, _irqs, _shared_isr, nqueues, cpu, std::move(on_trigger),
                        [this](std::uint16_t vector) {
                            const std::unique_lock _ { _lock };
                            return write_vector(regs::config_msix_vector, vector);
                        }
                    );
                }

                void enable_irqs() override
                {
                    pci::enable_irqs(*_dev, _irqs);
                }

                void release_irqs() override
                {
                    _irqs.release(*_dev);
                }

                std::uint8_t isr_status() override
                {
                    return _io.load(regs::isr_status);
                }
            };

            lib::expect<std::unique_ptr<transport_t>> make_transport(
                const std::shared_ptr<device> &dev
            )
            {
                const auto bars = dev->get_bars();
                if (bars.empty() || bars[0].type != bar::type::io)
                {
                    lib::error("virtio-pci: legacy device doesn't have io bar 0");
                    return std::unexpected { lib::err::not_supported };
                }

                if (bars[0].size < legacy::regs::config)
                {
                    lib::error("virtio-pci: legacy io bar 0 is too small (0x{:X} bytes)", bars[0].size);
                    return std::unexpected { lib::err::invalid_length };
                }

                return std::make_unique<transport_t>(
                    dev, arch::io_space { static_cast<std::uint16_t>(bars[0].phys) },
                    bars[0].size
                );
            }
        } // namespace legacy

        lib::expect<std::unique_ptr<transport_t>> make_transport(
            const std::shared_ptr<device> &dev
        )
        {
            std::array<std::optional<cap_info_t>, cfg_type_max> caps { };
            std::uint32_t notify_multiplier = 0;

            for (const auto &[id, offset] : dev->caps)
            {
                if (id != cap_id)
                    continue;

                const auto len = dev->read<8>(offset + cap::cap_len);
                if (len < cap::min_len)
                    continue;

                const auto type = dev->read<8>(offset + cap::cfg_type);
                if (type >= caps.size() || caps[type])
                    continue;

                if (type == cfg_type::notify_cfg)
                {
                    if (len < cap::notify_len)
                        continue;
                    notify_multiplier = dev->read<32>(offset + cap::notify_off_multiplier);
                }

                caps[type] = {
                    .bar = dev->read<8>(offset + cap::bar),
                    .offset = dev->read<32>(offset + cap::offset),
                    .length = dev->read<32>(offset + cap::length)
                };
            }

            if (!caps[cfg_type::common_cfg] || !caps[cfg_type::notify_cfg])
                return legacy::make_transport(dev);

            const auto common = map_region(dev, *caps[cfg_type::common_cfg]);
            if (!common)
                return std::unexpected { common.error() };

            if (common->length < regs::common_cfg_size)
            {
                lib::error("virtio-pci: common config is too small ({} bytes)", common->length);
                return std::unexpected { lib::err::invalid_length };
            }

            const auto notify = map_region(dev, *caps[cfg_type::notify_cfg]);
            if (!notify)
                return std::unexpected { notify.error() };

            region_t isr { };
            if (caps[cfg_type::isr_cfg])
            {
                if (const auto ret = map_region(dev, *caps[cfg_type::isr_cfg]))
                    isr = *ret;
            }

            region_t device_cfg { };
            if (caps[cfg_type::device_cfg])
            {
                if (const auto ret = map_region(dev, *caps[cfg_type::device_cfg]))
                    device_cfg = *ret;
            }

            return std::make_unique<pci_transport_t>(
                dev, *common, *notify, isr, device_cfg, notify_multiplier
            );
        }

        lib::expect<id_t> resolve_id(const std::shared_ptr<device> &dev)
        {
            if (dev->venid != ids::vendor)
                return std::unexpected { lib::err::no_such_device };

            if (dev->devid >= ids::modern_base && dev->devid <= ids::modern_last)
            {
                return id_t {
                    static_cast<std::uint32_t>(dev->devid - ids::modern_base),
                    dev->subvenid
                };
            }

            if (dev->devid >= ids::legacy_base && dev->devid <= ids::legacy_last)
            {
                if (dev->subdevid == 0)
                {
                    lib::error(
                        "virtio-pci: transitional device {:04X}:{:04X} has no subsystem id",
                        dev->venid, dev->devid
                    );
                    return std::unexpected { lib::err::no_such_device };
                }
                return id_t { dev->subdevid, dev->subvenid };
            }

            return std::unexpected { lib::err::no_such_device };
        }

        constexpr ::pci::id_t match_ids[] {
            ::pci::id_t::from_id(ids::vendor, ::pci::id_t::any)
        };

        struct driver_t final : ::pci::driver_t
        {
            lib::map::flat_hash<
                std::size_t,
                std::shared_ptr<device_t>
            > devs;

            driver_t() : ::pci::driver_t { "virtio-pci", match_ids } { }

            lib::expect<void> probe(::pci::device_t &pdev) override
            {
                const auto ident = resolve_id(pdev.dev);
                if (!ident)
                    return std::unexpected { ident.error() };

                pdev.dev->write<16>(
                    reg::cmd,
                    pdev.dev->read<16>(reg::cmd) | cmd::mem_space | cmd::io_space | cmd::bus_master
                );

                auto transport = make_transport(pdev.dev);
                if (!transport)
                    return std::unexpected { transport.error() };

                (*transport)->reset();
                (*transport)->add_status(status::acknowledge);
                (*transport)->add_status(status::driver);

                const auto type = static_cast<device_type>(ident->device);
                const auto num_queues = (*transport)->num_queues();
                lib::info(
                    "virtio-pci: found device: type: '{}', transport: '{}', {} queue{}",
                    magic_enum::enum_contains(type) ? magic_enum::enum_name(type) : "unknown",
                    (*transport)->type(), num_queues, num_queues != 1 ? "s" : ""
                );

                auto vdev = device_t::create(std::move(*transport), *ident, pdev.as_weak());
                if (const auto ret = dev::register_device(vdev); !ret)
                {
                    vdev->transport().add_status(status::failed);
                    return std::unexpected { ret.error() };
                }

                lib::bug_on(!devs.emplace(pdev.id, std::move(vdev)).second);
                return { };
            }

            bool remove(::pci::device_t &pdev) override
            {
                const auto it = devs.find(pdev.id);
                if (it == devs.end())
                    return false;

                dev::unregister_device(it->second);
                devs.erase(it);
                return true;
            }
        } drv;
    } // namespace

} // namespace virtio::pci

device_module(
    "virtio-pci", "Virtio Over PCI Bus",
    virtio::pci::drv, virtio::pci::match_ids
);
