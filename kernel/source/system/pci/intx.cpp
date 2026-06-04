// Copyright (C) 2024-2026  ilobilo

module system.pci;

namespace pci::intx
{
    lib::expect<irq::handle_t> request(
        pci::device &dev, std::size_t cpu_idx,
        irq::handler_fn fn, std::string_view name
    )
    {
        const auto route = dev.irq.route;
        if (!route)
            return std::unexpected { lib::err::not_supported };

        return irq::request_gsi(route->gsi, route->trig, cpu_idx, std::move(fn), name);
    }
} // namespace pci::intx
