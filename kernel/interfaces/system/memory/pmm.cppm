// Copyright (C) 2022-2024  ilobilo

export module system.memory.phys;
import std;

namespace pmm
{
    export constexpr std::size_t page_size = 0x1000;
    constexpr std::size_t max_order = 15;

    constexpr std::size_t page_bits = 12; // std::countr_zero(page_size)
    constexpr std::size_t paddr_bits = 48;
} // namespace pmm

export namespace pmm
{
    struct page
    {
        union {
            struct {
                std::uint64_t next_paddr : paddr_bits - page_bits;
                std::uint64_t order : std::bit_width(max_order);
                std::uint64_t allocated : 1;
            };
            std::uint64_t raw;
        };
        std::uint64_t raw2;
    };
    static_assert(sizeof(page) == 16);

    enum class type
    {
        sub1mib,
        sub4gib,
        normal
    };

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

    page *page_for(std::uintptr_t addr);

    template<typename Type> requires (std::is_pointer_v<Type>)
    inline page *page_for(Type ptr)
    {
        return page_for(reinterpret_cast<std::uintptr_t>(ptr));
    }

    [[nodiscard]]
    std::uintptr_t alloc(std::size_t count = 1, bool clear = false, type tp = type::normal);
    void free(std::uintptr_t addr, std::size_t count = 1);

    void reclaim_bootloader_memory();
    void init();
} // export namespace pmm