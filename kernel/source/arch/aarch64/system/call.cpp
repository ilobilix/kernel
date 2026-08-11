// Copyright (C) 2024-2026  ilobilo

module system.cpu.call;

namespace cpu
{
    void install_handler(std::size_t cpu_idx)
    {
        // TODO
        lib::unused(cpu_idx);
    }

    void notify_mask(const lib::bitmap_view mask)
    {
        if (mask.empty())
            return;

        // TODO
    }
} // namespace cpu
