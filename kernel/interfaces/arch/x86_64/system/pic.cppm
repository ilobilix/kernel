// Copyright (C) 2024-2025  ilobilo

export module x86_64.system.pic;
import std;

export namespace x86_64::pic
{
    void eoi(std::uint8_t vector);
    void mask(std::uint8_t vector);
    void unmask(std::uint8_t vector);

    void disable();
    void init();
} // export namespace x86_64::pic