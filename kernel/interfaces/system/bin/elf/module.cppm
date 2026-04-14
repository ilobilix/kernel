// Copyright (C) 2024-2025  ilobilo

export module system.bin.elf:mod;

import :symbols;
import lib;
import std;

export namespace bin::elf::mod
{
    enum class status { };

    struct initfini
    {
        std::uintptr_t init_array = 0;
        std::uintptr_t fini_array = 0;
        std::size_t init_array_size = 0;
        std::size_t fini_array_size = 0;

        void init();
        void fini();
    };

    struct entry
    {
        bool internal;

        std::vector<
            std::pair<
                std::uintptr_t,
                std::uintptr_t
            >
        > pages;

        ::mod::declare<0> *header;
        std::vector<std::string_view> deps;
        sym::symbol_table symbols;

        initfini initfini;

        status status;
    };

    lib::locker<
        lib::map::flat_hash<
            std::string_view,
            entry
        >, lib::rwspinlock
    > modules;

    lib::initgraph::stage *modules_loaded_stage();
} // export namespace bin::elf::mod