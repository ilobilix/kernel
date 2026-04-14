// Copyright (C) 2022-2024  ilobilo

module;

#include <arch/x86_64/system/cpu.hpp>

module x86_64.system.cpu;

import system.cpu.self;
import lib;

namespace cpu
{
    namespace features
    {
        constexpr std::uint64_t rfbm = ~0ull;
        constexpr std::uint32_t rfbm_low = rfbm & 0xFFFFFFFF;
        constexpr std::uint32_t rfbm_high = (rfbm >> 32) & 0xFFFFFFFF;

        void xsaveopt(std::byte *region)
        {
            asm volatile ("xsaveopt [%0]" :: "r"(region), "a"(rfbm_low), "d"(rfbm_high) : "memory");
        }

        void xsave(std::byte *region)
        {
            asm volatile ("xsave [%0]" :: "r"(region), "a"(rfbm_low), "d"(rfbm_high) : "memory");
        }

        void xrstor(std::byte *region)
        {
            asm volatile ("xrstor [%0]" :: "r"(region), "a"(rfbm_low), "d"(rfbm_high) : "memory");
        }

        void fxsave(std::byte *region)
        {
            asm volatile ("fxsave [%0]" :: "r"(region) : "memory");
        }

        void fxrstor(std::byte *region)
        {
            asm volatile ("fxrstor [%0]" :: "r"(region) : "memory");
        }

        void enable()
        {
            // SSE
            {
                auto cr0 = rdreg(cr0);
                cr0 = (cr0 & ~(1 << 2)) | (1 << 1);
                wrreg(cr0, cr0);

                auto cr4 = rdreg(cr4);
                cr4 |= (1 << 9) | (1 << 10);
                wrreg(cr4, cr4);
            }

            static std::uint32_t a1, b1, c1, d1;
            static const auto cached1 = [] { return cpu::id(0x01, 0, a1, b1, c1, d1); } ();

            static std::uint32_t a7, b7, c7, d7;
            static const auto cached7 = [] { return cpu::id(0x07, 0, a7, b7, c7, d7); } ();

            // UMIP SMEP SMAP INVPCID
            if (cached7)
            {
                auto cr4 = rdreg(cr4);
                {
                    if (c7 & (1 << 2))
                        cr4 |= (1 << 11);

                    if (b7 & (1 << 7))
                        cr4 |= (1 << 20);

                    if (b7 & (1 << 20))
                    {
                        cr4 |= (1 << 21);
                        smap::supported = true;
                    }

                    if ((c1 & (1 << 17)) && b7 & (1 << 10))
                    {
                        cr4 |= (1 << 17);
                        has_pcids = true;
                    }
                }
                wrreg(cr4, cr4);
            }

            // TSC
            {
                auto cr4 = rdreg(cr4);
                cr4 |= (1 << 2);
                wrreg(cr4, cr4);
            }

            if (cached1 && (c1 & (1 << 26)))
            {
                // xsave
                wrreg(cr4, rdreg(cr4) | (1 << 18));

                // x87 and SSE
                std::uint64_t xcr0 = 0b11;
                // AVX
                if (c1 & (1 << 28))
                    xcr0 |= (1 << 2);
                // AVX512
                if (cached7 && (b7 & (1 << 16)))
                    xcr0 |= (0b111 << 5);

                asm volatile ("xsetbv" :: "a"(xcr0), "d"(xcr0 >> 32), "c"(0) : "memory");

                if (fpu.save != nullptr && fpu.restore != nullptr)
                    return;

                std::uint32_t a, b, c, d;
                lib::ensure(cpu::id(0x0D, 0, a, b, c, d));

                bool xopt = cpu::id(0x0D, 1, a, b, c, d) && (a & (1 << 0));

                fpu.size = c;
                fpu.save = xopt ? xsaveopt : xsave;
                fpu.restore = xrstor;
            }
            else if (fpu.save == nullptr || fpu.restore == nullptr)
            {
                fpu.size = 512;
                fpu.save = fxsave;
                fpu.restore = fxrstor;
            }
        }
    } // namespace features
} // export namespace cpu