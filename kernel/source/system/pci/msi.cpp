// Copyright (C) 2024-2026  ilobilo

module system.pci;

import system.cpu;
import system.irq;
import lib;
import std;

namespace pci::msi
{
    namespace
    {
        constexpr std::uint8_t cap_id = 0x05;

        enum reg : std::uint16_t
        {
            msg_control = 2,
            msg_addr_low = 4,
            addr_high_64 = 8,
            data_64 = 12,
            mask_64 = 16,
            data_32 = 8,
            mask_32 = 12
        };

        enum mc : std::uint16_t
        {
            mc_enable = 1 << 0,
            mc_mme_shift = 4,
            mc_mme_mask = 0b111 << mc_mme_shift,
            mc_mmc_shift = 1,
            mc_mmc_mask = 0b111 << mc_mmc_shift,
            mc_addr64 = 1 << 7,
            mc_per_vec_mask = 1 << 8
        };

        lib::locker<
            lib::map::flat_hash<
                pci::device *,
                std::unique_ptr<msi_domain>
            >, lib::spinlock
        > domains;

        std::uint16_t find_msi_cap(const pci::device &dev)
        {
            for (const auto &[type, offset] : dev.caps)
            {
                if (type == cap_id)
                    return offset;
            }
            return 0;
        }

        struct cap_access
        {
            pci::device &dev;
            std::uint16_t base;

            template<typename Type>
            Type read(std::uint16_t off) const
            {
                return dev.read<Type>(base + off);
            }

            template<typename Type>
            void write(std::uint16_t off, Type val) const
            {
                dev.write<Type>(base + off, val);
            }
        };

        std::uint16_t mask_reg_off(bool addr64)
        {
            return addr64 ? reg::mask_64 : reg::mask_32;
        }

        std::uint16_t data_reg_off(bool addr64)
        {
            return addr64 ? reg::data_64 : reg::data_32;
        }
    } // namespace

    bool is_enabled(const pci::device &dev)
    {
        const auto off = find_msi_cap(dev);
        if (off == 0)
            return false;

        return (dev.read<std::uint16_t>(off + reg::msg_control) & mc_enable) != 0;
    }

    msi_domain::msi_domain(pci::device &dev, std::uint16_t cap_offset, irq::domain *parent)
        : domain { "pci-msi", parent }, _dev { &dev },
          _cap_offset { cap_offset }, _allocated_vecs { 0 }
    {
        const auto mc_val = dev.read<std::uint16_t>(cap_offset + reg::msg_control);
        _addr64 = (mc_val & mc_addr64) != 0;
        _per_vector_mask = (mc_val & mc_per_vec_mask) != 0;
    }

    msi_domain::~msi_domain()
    {
        lib::bug_on(_allocated_vecs != 0);
        lib::bug_on(!_spare_parents.empty());
    }

    lib::expect<void> msi_domain::alloc(
        std::span<irq::irq_data *> data, const irq::fwspec &spec
    )
    {
        if (data.empty())
            return { };

        const auto count = lib::next_pow2(data.size());
        const auto log2 = lib::log2(count);

        const auto mc_now = _dev->read<std::uint16_t>(_cap_offset + reg::msg_control);
        if (log2 > ((mc_now & mc_mmc_mask) >> mc_mmc_shift))
            return std::unexpected { lib::err::invalid_argument };

        {
            const std::unique_lock _ { _lock };
            if (_allocated_vecs != 0)
                return std::unexpected { lib::err::target_is_busy };
            _allocated_vecs = data.size();
        }

        const auto cpu_idx = (spec.param_count > param_cpu)
            ? spec.params[param_cpu]
            : static_cast<std::uint32_t>(cpu::bsp_idx());

        std::vector<std::unique_ptr<irq::irq_data>> parents;
        std::vector<irq::irq_data *> parent_ptrs;

        parents.reserve(count);
        parent_ptrs.reserve(count);

        for (std::size_t i = 0; i < count; i++)
        {
            auto pd = std::make_unique<irq::irq_data>();
            pd->virq = i < data.size() ? data[i]->virq : 0;
            pd->dom = parent;

            parent_ptrs.push_back(pd.get());
            parents.push_back(std::move(pd));
        }

        const irq::fwspec pspec {
            .param_count = 2,
            .params = { cpu_idx, 0 }
        };

        if (auto ret = parent->alloc(parent_ptrs, pspec); !ret)
        {
            const std::unique_lock _ { _lock };
            _allocated_vecs = 0;
            return std::unexpected { ret.error() };
        }

        const auto msg = parent->compose_msi(*parents[0]);
        if (!msg)
        {
            parent->free(parent_ptrs);
            const std::unique_lock _ { _lock };
            _allocated_vecs = 0;
            return std::unexpected { msg.error() };
        }

        const cap_access cap { *_dev, _cap_offset };
        {
            const std::unique_lock _ { _lock };

            cap.write<std::uint32_t>(reg::msg_addr_low, msg->address);
            if (_addr64)
                cap.write<std::uint32_t>(reg::addr_high_64, msg->address >> 32);
            cap.write<std::uint16_t>(data_reg_off(_addr64), msg->data);

            if (_per_vector_mask)
                cap.write<std::uint32_t>(mask_reg_off(_addr64), ~0u);

            auto cmd = _dev->read<std::uint16_t>(pci::reg::cmd);
            cmd |= static_cast<std::uint16_t>(pci::cmd::int_dis);
            _dev->write<std::uint16_t>(pci::reg::cmd, cmd);

            auto mc_val = cap.read<std::uint16_t>(reg::msg_control);
            mc_val &= ~mc_mme_mask;
            mc_val |= static_cast<std::uint16_t>(log2 << mc_mme_shift);
            mc_val |= mc_enable;
            cap.write<std::uint16_t>(reg::msg_control, mc_val);
        }

        for (std::size_t i = 0; i < data.size(); i++)
        {
            data[i]->hwirq = i;
            data[i]->aux = cpu_idx;
            data[i]->trig = irq::trigger::edge_rising;
            data[i]->parent = parents[i].release();
        }

        for (std::size_t i = data.size(); i < count; i++)
            _spare_parents.push_back(parents[i].release());
        return { };
    }

    void msi_domain::free(std::span<irq::irq_data *> data)
    {
        for (auto entry : data)
        {
            const cap_access cap { *_dev, _cap_offset };
            {
                const std::unique_lock _ { _lock };

                if (_per_vector_mask)
                {
                    auto mask = cap.read<std::uint32_t>(mask_reg_off(_addr64));
                    mask |= (1u << entry->hwirq);
                    cap.write<std::uint32_t>(mask_reg_off(_addr64), mask);
                }

                if (_allocated_vecs > 0)
                    _allocated_vecs--;

                if (_allocated_vecs == 0)
                {
                    auto mc_val = cap.read<std::uint16_t>(reg::msg_control);
                    mc_val &= ~mc_enable;
                    mc_val &= ~mc_mme_mask;
                    cap.write<std::uint16_t>(reg::msg_control, mc_val);

                    if (!_spare_parents.empty())
                    {
                        parent->free(_spare_parents);
                        for (auto pd : _spare_parents)
                            delete pd;
                        _spare_parents.clear();
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

    void msi_domain::_mask(std::uintptr_t hwirq)
    {
        const cap_access cap { *_dev, _cap_offset };
        if (_per_vector_mask)
        {
            auto mask = cap.read<std::uint32_t>(mask_reg_off(_addr64));
            mask |= (1u << hwirq);
            cap.write<std::uint32_t>(mask_reg_off(_addr64), mask);
        }
        else
        {
            auto mc_val = cap.read<std::uint16_t>(reg::msg_control);
            mc_val &= ~mc_enable;
            cap.write<std::uint16_t>(reg::msg_control, mc_val);
        }
    }

    void msi_domain::_unmask(std::uintptr_t hwirq)
    {
        const cap_access cap { *_dev, _cap_offset };
        if (_per_vector_mask)
        {
            auto mask = cap.read<std::uint32_t>(mask_reg_off(_addr64));
            mask &= ~(1u << hwirq);
            cap.write<std::uint32_t>(mask_reg_off(_addr64), mask);
        }
        else
        {
            auto mc_val = cap.read<std::uint16_t>(reg::msg_control);
            mc_val |= mc_enable;
            cap.write<std::uint16_t>(reg::msg_control, mc_val);
        }
    }

    void msi_domain::mask(irq::irq_data &data)
    {
        const std::unique_lock _ { _lock };
        _mask(data.hwirq);
    }

    void msi_domain::unmask(irq::irq_data &data)
    {
        const std::unique_lock _ { _lock };
        _unmask(data.hwirq);
    }

    lib::expect<void> msi_domain::set_affinity(
        irq::irq_data &data, const lib::bitmap &cpus, bool force
    )
    {
        if (!data.parent)
            return std::unexpected { lib::err::invalid_argument };

        {
            const std::unique_lock _ { _lock };
            _mask(data.hwirq);
        }

        auto ret = parent->set_affinity(*data.parent, cpus, force);
        if (!ret)
        {
            const std::unique_lock _ { _lock };
            _unmask(data.hwirq);
            return ret;
        }

        const auto msg = parent->compose_msi(*data.parent);
        if (!msg)
            return std::unexpected { msg.error() };

        {
            const std::unique_lock _ { _lock };
            const cap_access cap { *_dev, _cap_offset };

            cap.write<std::uint32_t>(reg::msg_addr_low, msg->address);
            if (_addr64)
                cap.write<std::uint32_t>(reg::addr_high_64, msg->address >> 32);
            cap.write<std::uint16_t>(data_reg_off(_addr64), msg->data);

            _unmask(data.hwirq);
        }
        return { };
    }

    void release(pci::device &dev)
    {
        std::unique_ptr<msi_domain> dom;
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

    msi_domain *for_device(pci::device &dev)
    {
        const auto cap_offset = find_msi_cap(dev);
        if (cap_offset == 0)
            return nullptr;

        if (pci::msix::is_enabled(dev))
            return nullptr;

        auto parent = irq::msi_parent();
        if (!parent)
            return nullptr;

        auto locked = domains.lock();
        if (const auto it = locked->find(&dev); it != locked->end())
            return it->second.get();

        auto dom = std::make_unique<msi_domain>(dev, cap_offset, parent);
        auto raw = dom.get();
        locked->emplace(&dev, std::move(dom));
        return raw;
    }

    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx,
        irq::handler_fn fn, std::string_view name
    )
    {
        auto dom = for_device(dev);
        if (!dom)
            return std::unexpected { lib::err::not_supported };

        const irq::fwspec spec {
            .param_count = msi_domain::param_count,
            .params = {
                [msi_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx)
            }
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
            .param_count = msi_domain::param_count,
            .params = {
                [msi_domain::param_cpu] = static_cast<std::uint32_t>(cpu_idx)
            }
        };
        return irq::alloc_num(*dom, spec, count);
    }
} // namespace pci::msi
