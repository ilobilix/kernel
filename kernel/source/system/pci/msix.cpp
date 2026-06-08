// Copyright (C) 2024-2026  ilobilo

module system.pci;

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

        // clang-format off
        lib::locker<
            lib::map::flat_hash<
                pci::device *,
                std::unique_ptr<msix_domain>
            >, lib::spinlock
        > domains;
        // clang-format on

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

    msix_domain::msix_domain(pci::device &dev, std::uint16_t cap_offset, irq::domain *parent)
        : domain { "pci-msix", parent }, _dev { &dev }, _cap_offset { cap_offset }, _live_count { 0 }
    {
        const auto mc_val = dev.read<std::uint16_t>(cap_offset + reg::msg_control);
        _nvec = (mc_val & mc_table_size_mask) + 1;
        _allocated = lib::bitmap { _nvec };

        const auto tbl_desc = dev.read<std::uint32_t>(cap_offset + reg::table);
        const auto bir = tbl_desc & tbl::bir_mask;
        const auto offset = tbl_desc & tbl::offset_mask;

        lib::bug_on(bir >= dev.bars.size());
        lib::bug_on(dev.bars[bir].type != pci::bar::type::mem);
        _table = dev.bars[bir].map() + offset;
    }

    lib::expect<void> msix_domain::alloc(std::span<irq::irq_data *> data, const irq::fwspec &spec)
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

            const irq::fwspec pspec { .param_count = 2, .params = { cpu_idx, 0 } };

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
        lib::mmio::out<32>(entry_addr(_table, data.hwirq) + entry::vec_control, vec_control_mask);
    }

    void msix_domain::unmask(irq::irq_data &data)
    {
        lib::mmio::out<32>(entry_addr(_table, data.hwirq) + entry::vec_control, 0);
    }

    lib::expect<void> msix_domain::set_affinity(
        irq::irq_data &data, const lib::bitmap &cpus, bool force
    )
    {
        if (!data.parent)
            return std::unexpected { lib::err::invalid_argument };

        const auto idx = data.hwirq;
        const auto ea = entry_addr(_table, idx);

        lib::mmio::out<32>(ea + entry::vec_control, vec_control_mask);

        if (auto ret = parent->set_affinity(*data.parent, cpus, force); !ret)
        {
            lib::mmio::out<32>(ea + entry::vec_control, 0);
            return ret;
        }

        const auto msg = parent->compose_msi(*data.parent);
        if (!msg)
            return std::unexpected { msg.error() };

        lib::mmio::out<32>(ea + entry::msg_addr_low, msg->address);
        lib::mmio::out<32>(ea + entry::msg_addr_high, msg->address >> 32);
        lib::mmio::out<32>(ea + entry::msg_data, msg->data);
        lib::mmio::out<32>(ea + entry::vec_control, 0);

        return { };
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

        auto locked = domains.lock();
        if (const auto it = locked->find(&dev); it != locked->end())
            return it->second.get();

        auto dom = std::make_unique<msix_domain>(dev, cap_offset, parent);
        auto raw = dom.get();
        locked->emplace(&dev, std::move(dom));
        return raw;
    }

    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx, irq::handler_fn fn, std::string_view name
    )
    {
        auto dom = for_device(dev);
        if (!dom)
            return std::unexpected { lib::err::not_supported };

        const irq::fwspec spec {
            .param_count = msix_domain::param_count,
            .params = { [msix_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx) }
        };
        return irq::alloc_and_request(*dom, spec, std::move(fn), name);
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
            .params = { [msix_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx) }
        };
        return irq::alloc_num(*dom, spec, count);
    }
} // namespace pci::msix
