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

        auto trig = irq::trigger::edge_rising;
        const bool low = (route->flags & pci::router::low) != 0;
        if ((route->flags & pci::router::level) != 0)
            trig = low ? irq::trigger::level_low : irq::trigger::level_high;
        else
            trig = low ? irq::trigger::edge_falling : irq::trigger::edge_rising;

        return irq::request_gsi(route->gsi, trig, cpu_idx, std::move(fn), name);
    }
} // namespace pci::intx
