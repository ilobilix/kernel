// Copyright (C) 2022-2024  ilobilo

#pragma once

#define mrs(reg) ({ std::uint64_t value; asm volatile ("mrs %0, " #reg : "=r"(value) :: "memory"); value; })
#define msr(reg, value) asm volatile ("msr " #reg ", %0" :: "r"(value) : "memory")