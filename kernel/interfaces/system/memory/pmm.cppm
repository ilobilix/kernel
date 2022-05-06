// Copyright (C) 2022-2024  ilobilo

export module system.memory.phys;
import std;

export namespace pmm
{
    constexpr std::size_t page_size = 0x1000;

    struct memory
    {
        std::size_t top;
        std::size_t usable_top;

        std::size_t total;
        std::size_t usable;
        std::size_t used;
    };
    memory info();

    void *alloc(std::size_t count = 1);
    void free(void *ptr, std::size_t count = 1);

    template<typename Type = void *>
    inline Type alloc(std::size_t count = 1)
    {
        return reinterpret_cast<Type>(alloc(count));
    }

    inline void free(auto ptr, std::size_t count = 1)
    {
        return free(reinterpret_cast<void *>(ptr), count);
    }

    void reclaim();
    void init();
} // export namespace pmm