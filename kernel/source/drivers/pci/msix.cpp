// Copyright (C) 2024-2026  ilobilo

module drivers.pci;

import system.cpu;

namespace pci::msix
{
    namespace
    {
        constexpr std::uint8_t cap_id = 0x11;
        constexpr std::uint32_t vec_control_mask = 1 << 0;

        enum reg : std::uint16_t
        {
            msg_control = 2,
            table = 4
        };

        enum mc : std::uint16_t
        {
            mc_table_size_mask = 0x07FF,
            mc_function_mask = 1 << 14,
            mc_enable = 1 << 15
        };

        enum tbl : std::uint32_t
        {
            bir_mask = 0x7,
            offset_mask = ~0x7u
        };

        enum entry : std::size_t
        {
            stride = 16,
            msg_addr_low = 0,
            msg_addr_high = 4,
            msg_data = 8,
            vec_control = 12
        };

        lib::locker<
            lib::map::flat_hash<
                pci::device *,
                std::unique_ptr<msix_domain>
            >, lib::spinlock
        > domains;

        std::uint16_t find_msix_cap(const pci::device &dev)
        {
            for (const auto &[type, offset] : dev.caps)
            {
                if (type == cap_id)
                    return offset;
            }
            return 0;
        }

        std::uintptr_t entry_addr(std::uintptr_t table, std::size_t idx)
        {
            return table + idx * entry::stride;
        }
    } // namespace

    bool is_enabled(const pci::device &dev)
    {
        const auto off = find_msix_cap(dev);
        if (off == 0)
            return false;

        return (dev.read<std::uint16_t>(off + reg::msg_control) & mc_enable) != 0;
    }

    auto msix_domain::resolve_table(pci::device &dev, std::uint16_t cap_offset)
        -> lib::expect<table_info>
    {
        const auto tbl_desc = dev.read<std::uint32_t>(cap_offset + reg::table);
        const auto bir = tbl_desc & tbl::bir_mask;
        const auto offset = tbl_desc & tbl::offset_mask;

        if (bir >= dev.bars.size())
        {
            lib::warn("pci-msix: table bir {} is out of range", bir);
            return std::unexpected { lib::err::not_supported };
        }

        auto &bar = dev.bars[bir];
        if (bar.type != pci::bar::type::mem)
        {
            lib::warn("pci-msix: table bar {} is not mmio", bir);
            return std::unexpected { lib::err::not_supported };
        }

        if (offset + entry::stride > bar.size)
        {
            lib::warn("pci-msix: table offset 0x{:X} is outside bar {}", offset, bir);
            return std::unexpected { lib::err::not_supported };
        }

        if (!(dev.read<std::uint16_t>(pci::reg::cmd) & pci::cmd::mem_space))
        {
            lib::warn("pci-msix: memory space is disabled");
            return std::unexpected { lib::err::not_supported };
        }

        const auto mc_val = dev.read<std::uint16_t>(cap_offset + reg::msg_control);
        const std::uint16_t nvec = (mc_val & mc_table_size_mask) + 1;

        if (offset + static_cast<std::size_t>(nvec) * entry::stride > bar.size)
        {
            lib::warn("pci-msix: {} entry table does not fit in bar {}", nvec, bir);
            return std::unexpected { lib::err::not_supported };
        }

        return table_info { bar.map() + offset, nvec };
    }

    msix_domain::msix_domain(
        pci::device &dev, std::uint16_t cap_offset,
        const table_info &table, irq::domain *parent
    ) : domain { "pci-msix", parent }, _dev { &dev }, _cap_offset { cap_offset },
        _nvec { table.nvec }, _table { table.base }, _intx_dis { false }, _allocated { _nvec },
        _live_count { 0 } { }

    lib::expect<void> msix_domain::alloc(
        std::span<irq::irq_data *> data, const irq::fwspec &spec
    )
    {
        if (data.empty())
            return { };

        const auto cpu_idx = (spec.param_count > param_cpu)
            ? spec.params[param_cpu]
            : static_cast<std::uint32_t>(cpu::bsp_idx());

        std::vector<std::size_t> picked;
        picked.reserve(data.size());
        {
            const std::unique_lock _ { _lock };
            for (std::size_t i = 0; i < _nvec && picked.size() < data.size(); i++)
            {
                if (!_allocated.get(i))
                {
                    _allocated.set(i, true);
                    picked.push_back(i);
                }
            }

            if (picked.size() < data.size())
            {
                for (auto i : picked)
                    _allocated.set(i, false);
                return std::unexpected { lib::err::no_space_left };
            }
        }

        std::vector<std::unique_ptr<irq::irq_data>> parents;
        parents.reserve(data.size());

        auto cleanup = [&] {
            for (auto &pd : parents)
            {
                irq::irq_data *p = pd.get();
                parent->free({ &p, 1 });
            }
            parents.clear();

            const std::unique_lock _ { _lock };
            for (auto i : picked)
                _allocated.set(i, false);
        };

        for (std::size_t i = 0; i < data.size(); i++)
        {
            auto pd = std::make_unique<irq::irq_data>();
            pd->virq = data[i]->virq;
            pd->dom = parent;

            const irq::fwspec pspec {
                .param_count = 2,
                .params = { cpu_idx, 0 }
            };

            auto pd_ptr = pd.get();
            if (auto ret = parent->alloc({ &pd_ptr, 1 }, pspec); !ret)
            {
                cleanup();
                return std::unexpected { ret.error() };
            }
            parents.push_back(std::move(pd));
        }

        for (std::size_t i = 0; i < data.size(); i++)
        {
            const auto msg = parent->compose_msi(*parents[i]);
            if (!msg)
            {
                cleanup();
                return std::unexpected { msg.error() };
            }

            const auto ea = entry_addr(_table, picked[i]);
            lib::mmio::out<32>(ea + entry::vec_control, vec_control_mask);
            lib::mmio::out<32>(ea + entry::msg_addr_low, msg->address);
            lib::mmio::out<32>(ea + entry::msg_addr_high, msg->address >> 32);
            lib::mmio::out<32>(ea + entry::msg_data, msg->data);
        }

        {
            const std::unique_lock _ { _lock };

            if (_live_count == 0)
            {
                auto mc_val = _dev->read<std::uint16_t>(_cap_offset + reg::msg_control);
                mc_val &= ~mc_function_mask;
                mc_val |= mc_enable;
                _dev->write<std::uint16_t>(_cap_offset + reg::msg_control, mc_val);

                auto cmd = _dev->read<std::uint16_t>(pci::reg::cmd);
                if (!(cmd & pci::cmd::bus_master))
                    lib::warn("pci-msix: bus mastering is disabled");
                _intx_dis = (cmd & pci::cmd::int_dis) != 0;
                cmd |= static_cast<std::uint16_t>(pci::cmd::int_dis);
                _dev->write<std::uint16_t>(pci::reg::cmd, cmd);
            }
            _live_count += data.size();
        }

        for (std::size_t i = 0; i < data.size(); i++)
        {
            data[i]->hwirq = picked[i];
            data[i]->aux = cpu_idx;
            data[i]->trig = irq::trigger::edge_rising;
            data[i]->parent = parents[i].release();
        }
        return { };
    }

    void msix_domain::free(std::span<irq::irq_data *> data)
    {
        for (auto entry : data)
        {
            const auto idx = entry->hwirq;
            const auto ea = entry_addr(_table, idx);

            lib::mmio::out<32>(ea + entry::vec_control, vec_control_mask);
            lib::mmio::out<32>(ea + entry::msg_addr_low, 0);
            lib::mmio::out<32>(ea + entry::msg_addr_high, 0);
            lib::mmio::out<32>(ea + entry::msg_data, 0);

            {
                const std::unique_lock _ { _lock };
                _allocated.set(idx, false);

                if (_live_count > 0)
                    _live_count--;

                if (_live_count == 0)
                {
                    auto mc_val = _dev->read<std::uint16_t>(_cap_offset + reg::msg_control);
                    mc_val &= ~mc_enable;
                    _dev->write<std::uint16_t>(_cap_offset + reg::msg_control, mc_val);

                    if (!_intx_dis)
                    {
                        auto cmd = _dev->read<std::uint16_t>(pci::reg::cmd);
                        cmd &= ~static_cast<std::uint16_t>(pci::cmd::int_dis);
                        _dev->write<std::uint16_t>(pci::reg::cmd, cmd);
                    }
                }
            }

            std::unique_ptr<irq::irq_data> pd { entry->parent };
            entry->parent = nullptr;
            if (pd)
            {
                auto pd_ptr = pd.get();
                parent->free({ &pd_ptr, 1 });
            }
        }
    }

    void msix_domain::mask(irq::irq_data &data)
    {
        const auto addr = entry_addr(_table, data.hwirq) + entry::vec_control;
        lib::mmio::out<32>(addr, lib::mmio::in<32>(addr) | vec_control_mask);
    }

    void msix_domain::unmask(irq::irq_data &data)
    {
        const auto addr = entry_addr(_table, data.hwirq) + entry::vec_control;
        lib::mmio::out<32>(addr, lib::mmio::in<32>(addr) & ~vec_control_mask);
    }

    lib::expect<void> msix_domain::set_affinity(
        irq::irq_data &data, const lib::bitmap_view cpus, bool force
    )
    {
        if (!data.parent)
            return std::unexpected { lib::err::invalid_argument };

        const auto idx = data.hwirq;
        const auto ea = entry_addr(_table, idx);

        const auto old_control = lib::mmio::in<32>(ea + entry::vec_control);
        const bool was_masked = (old_control & vec_control_mask) != 0;

        lib::mmio::out<32>(ea + entry::vec_control, old_control | vec_control_mask);

        return irq::retarget(*parent, data, cpus, force, "pci-msix", [&] -> lib::expect<void> {
            const auto msg = parent->compose_msi(*data.parent);
            if (!msg)
                return std::unexpected { msg.error() };

            lib::mmio::out<32>(ea + entry::msg_addr_low, msg->address);
            lib::mmio::out<32>(ea + entry::msg_addr_high, msg->address >> 32);
            lib::mmio::out<32>(ea + entry::msg_data, msg->data);
            if (!was_masked)
                lib::mmio::out<32>(ea + entry::vec_control, old_control & ~vec_control_mask);
            return { };
        });
    }

    void release(pci::device &dev)
    {
        std::unique_ptr<msix_domain> dom;
        {
            auto locked = domains.lock();
            const auto it = locked->find(&dev);
            if (it == locked->end())
                return;

            lib::bug_on(it->second->live_count() != 0);
            dom = std::move(it->second);
            locked->erase(it);
        }
    }

    lib::expect<std::uint16_t> vector_of(pci::device &dev, irq::handle_t handle)
    {
        msix_domain *dom = nullptr;
        {
            auto locked = domains.lock();
            if (const auto it = locked->find(&dev); it != locked->end())
                dom = it->second.get();
        }

        if (!dom)
            return std::unexpected { lib::err::not_supported };
        return irq::hwirq_of(handle, dom);
    }

    msix_domain *for_device(pci::device &dev)
    {
        const auto cap_offset = find_msix_cap(dev);
        if (cap_offset == 0)
            return nullptr;

        if (pci::msi::is_enabled(dev))
            return nullptr;

        auto parent = irq::msi_parent();
        if (!parent)
            return nullptr;

        {
            auto locked = domains.lock();
            if (const auto it = locked->find(&dev); it != locked->end())
                return it->second.get();
        }

        const auto table = msix_domain::resolve_table(dev, cap_offset);
        if (!table)
            return nullptr;

        auto dom = std::make_unique<msix_domain>(dev, cap_offset, *table, parent);

        auto locked = domains.lock();
        if (const auto it = locked->find(&dev); it != locked->end())
            return it->second.get();

        auto raw = dom.get();
        locked->emplace(&dev, std::move(dom));
        return raw;
    }

    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx,
        irq::handler_fn fn, std::string_view name, const void *owner
    )
    {
        auto dom = for_device(dev);
        if (!dom)
            return std::unexpected { lib::err::not_supported };

        const irq::fwspec spec {
            .param_count = msix_domain::param_count,
            .params = {
                [msix_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx)
            }
        };
        return irq::alloc_and_request(*dom, spec, std::move(fn), name, true, owner);
    }

    lib::expect<std::vector<irq::handle_t>> alloc(
        pci::device &dev, std::size_t count, std::size_t cpu_idx
    )
    {
        auto dom = for_device(dev);
        if (!dom)
            return std::unexpected { lib::err::not_supported };

        const irq::fwspec spec {
            .param_count = msix_domain::param_count,
            .params = {
                [msix_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx)
            }
        };
        return irq::alloc(*dom, spec, count);
    }
} // namespace pci::msix
