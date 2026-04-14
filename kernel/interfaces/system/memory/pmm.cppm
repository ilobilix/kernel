// Copyright (C) 2022-2024  ilobilo

export module system.memory.phys;
import cppstd;

export namespace pmm
{
    constexpr std::size_t page_size = 0x1000;

    struct memory
    {
        std::size_t top = 0;
        std::size_t usable_top = 0;

        std::size_t total = 0;
        std::size_t usable = 0;
        std::size_t used = 0;
    };
    memory info();

    void *alloc(std::size_t count = 1, bool clear = false);
    void free(void *ptr, std::size_t count = 1);

    template<typename Type = void *>
    inline Type alloc(std::size_t count = 1, bool clear = false)
    {
        return reinterpret_cast<Type>(alloc(count, clear));
    }

    inline void free(auto ptr, std::size_t count = 1)
    {
        return free(reinterpret_cast<void *>(ptr), count);
    }

    void reclaim();
    void init();
} // export namespace pmm