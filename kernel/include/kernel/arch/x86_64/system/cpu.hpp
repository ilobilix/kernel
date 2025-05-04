// Copyright (C) 2022-2024  ilobilo

#pragma once

#define rdreg(reg)                                             \
({                                                             \
    std::uintptr_t val;                                        \
    asm volatile ("mov %0, " #reg "" : "=r"(val) :: "memory"); \
    val;                                                       \
})

#define wrreg(reg, val) asm volatile ("mov " #reg ", %0" :: "r"(val) : "memory")