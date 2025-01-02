// Copyright (C) 2024-2025  ilobilo

module system.memory.virt;

import system.memory.phys;
import lib;
import std;

import :pagemap;

namespace vmm
{
    std::uintptr_t pagemap::pa_mask = 0;

    const std::uintptr_t pagemap::valid_table_flags = 0;
    const std::uintptr_t pagemap::new_table_flags = 0;

    struct pagemap::table { };

    auto pagemap::new_table() -> table *
    {
        return pmm::alloc<table *>();
    }

    page_size pagemap::fixpsize(page_size psize) { return psize; }
    void pagemap::invalidate(std::uintptr_t vaddr) { lib::unused(vaddr); }

    std::uintptr_t pagemap::to_arch(flag flags, caching cache, page_size psize) { lib::unused(flags, cache, psize); return 0; }

    auto pagemap::from_arch(std::uintptr_t flags, page_size psize) -> std::pair<flag, caching>
    {
        lib::unused(flags, psize);
        return { flag::rw, caching::normal };
    }

    std::size_t pagemap::from_page_size(page_size psize) { lib::unused(psize); return 0; }
    page_size pagemap::max_page_size(std::size_t size) { lib::unused(size); return page_size::small; }

    auto pagemap::getpte(std::uintptr_t vaddr, page_size psize, bool allocate) -> std::expected<std::reference_wrapper<entry>, error>
    {
        lib::unused(vaddr, psize, allocate);
        return std::unexpected { error::invalid_entry };
    }

    void pagemap::store() { }
    void pagemap::load() const { }

    pagemap::pagemap() : _table { new_table() }
    {
        lib::panic("TODO");
    }

    pagemap::~pagemap() { lib::panic("TODO"); }
} // namespace vmm