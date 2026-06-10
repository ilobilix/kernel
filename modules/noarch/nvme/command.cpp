// Copyright (C) 2024-2026  ilobilo

module nvme;

import system.memory.phys;

namespace nvme
{
    void command_t::setup(arch::dma_buffer_view view)
    {
        _prps.clear();

        auto vaddr = reinterpret_cast<std::uintptr_t>(view.data());
        const auto offset = vaddr % pmm::page_size;

        if (offset + view.size() < pmm::page_size * 2)
        {
            _cmd.common.data_ptr.prp1 = lib::fromhh(vaddr);
            _cmd.common.data_ptr.prp2 = 0;

            const auto first_len = pmm::page_size - offset;
            if (view.size() > first_len)
            {
                const auto vaddr = reinterpret_cast<std::uintptr_t>(view.subview(first_len).data());
                _cmd.common.data_ptr.prp2 = lib::fromhh(vaddr);
            }
            return;
        }

        const auto prp1 = lib::fromhh(vaddr);
        auto size = view.size();

        size -= pmm::page_size - offset;
        vaddr += pmm::page_size - offset;

        if (size <= pmm::page_size)
        {
            _cmd.common.data_ptr.prp1 = prp1;
            _cmd.common.data_ptr.prp2 = lib::fromhh(vaddr);
            return;
        }

        arch::dma_array<std::uint64_t> prp { &_pool, pmm::page_size >> 3 };
        auto prp_list = prp.data();

        const auto prp2 = lib::fromhh(reinterpret_cast<std::uintptr_t>(prp_list));
        _prps.push_back(std::move(prp));

        std::size_t i = 0;
        while (true)
        {
            if (i == pmm::page_size >> 3)
            {
                auto old_prp = prp_list;
                prp = decltype(prp) { &_pool, pmm::page_size >> 3 };
                prp_list = prp.data();
                _prps.push_back(std::move(prp));

                prp_list[0] = old_prp[i - 1];
                old_prp[i - 1] = lib::fromhh(reinterpret_cast<std::uintptr_t>(prp_list));
                i = 1;
            }

            prp_list[i++] = lib::fromhh(vaddr);
            vaddr += pmm::page_size;

            if (size <= pmm::page_size)
                break;
            size -= pmm::page_size;
        }

        _cmd.common.data_ptr.prp1 = prp1;
        _cmd.common.data_ptr.prp2 = prp2;
    }
} // namespace nvme
