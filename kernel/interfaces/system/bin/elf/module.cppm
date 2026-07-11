// Copyright (C) 2024-2026  ilobilo

export module system.bin.elf:mod;

import :symbols;
import lib;
import std;

export namespace bin::elf::mod
{
    struct initfini_t
    {
        std::uintptr_t init_array = 0;
        std::uintptr_t fini_array = 0;
        std::size_t init_array_size = 0;
        std::size_t fini_array_size = 0;

        bool inited = false;
        bool finied = false;

        void init();
        void fini();
    };

    enum class status
    {
        loaded,
        activating,
        active,
        failed
    };

    struct alias_t
    {
        std::string pattern;

        bool match(std::string_view modalias) const;
    };

    struct image_t
    {
        std::vector<
            std::pair<
                std::uintptr_t,
                std::uintptr_t
            >
        > pages;
        sym::symbol_table symbols;
        initfini_t initfini;

        ~image_t();
    };

    struct entry_t
    {
        bool internal;
        std::shared_ptr<image_t> image;

        ::mod::declare<0, 0> *header;
        std::vector<alias_t> aliases;

        status status;
        std::size_t dependents;
    };

    lib::locker<
        lib::map::flat_hash<
            std::string_view,
            std::shared_ptr<entry_t>
        >, lib::rwspinlock
    > modules;

    std::atomic<std::uint64_t> generation { 0 };

    bool request_alias(std::string_view modalias);
    bool unload(std::string_view name);

    lib::initgraph::stage *modules_loaded_stage();
} // export namespace bin::elf::mod
