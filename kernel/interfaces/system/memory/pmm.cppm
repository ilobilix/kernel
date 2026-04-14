// Copyright (C) 2024-2026  ilobilo

export module system.memory.phys;
import std;

export namespace pmm
{
    constexpr std::size_t page_size = 0x1000;
    constexpr std::size_t max_order = 15;

    constexpr std::size_t page_bits = std::countr_zero(page_size);
    constexpr std::size_t paddr_bits = 48;

    struct memory
    {
        std::uintptr_t top = 0;
        std::uintptr_t usable_top = 0;

        std::uintptr_t pfndb_base = 0;
        std::uintptr_t pfndb_end = 0;

        std::size_t usable = 0;
        std::size_t used = 0;

        std::uintptr_t free_start() const { return pfndb_end; }
    };
    memory info();

    enum class type
    {
        sub1mib,
        sub4gib,
        normal
    };

    [[nodiscard]]
    std::uintptr_t alloc(std::size_t count = 1, bool clear = false, type tp = type::normal);
    void free(std::uintptr_t addr, std::size_t count = 1);

    void reclaim_bootloader_memory();
    void init();
} // export namespace pmm
