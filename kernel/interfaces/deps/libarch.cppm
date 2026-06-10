// Copyright (C) 2024-2026  ilobilo

module;

#include <arch/bits.hpp>
#include <arch/io_space.hpp>
#include <arch/mem_space.hpp>
#include <arch/register.hpp>
#include <arch/variable.hpp>

#include <arch/dma_pool.hpp>
#include <arch/dma_structs.hpp>

export module libarch;

export namespace arch
{
    using ::arch::bit_value;
    using ::arch::field;

    using ::arch::io_space;

    using ::arch::io_mem_space;
    using ::arch::main_mem_space;
    using ::arch::mem_space;

    using ::arch::scalar_register;
    using ::arch::bit_register;

    using ::arch::scalar_variable;
    using ::arch::bit_variable;

    using ::arch::scalar_storage;
    using ::arch::bit_storage;

    using ::arch::dma_region;
    using ::arch::dma_ptr;

    using ::arch::contiguous_pool;
    using ::arch::dma_pool;

    using ::arch::dma_buffer;
    using ::arch::dma_buffer_view;
    using ::arch::dma_object;
    using ::arch::dma_object_view;
    using ::arch::dma_array;
    using ::arch::dma_array_view;
} // export namespace arch
