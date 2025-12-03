// Copyright (C) 2022-2024  ilobilo

module;

#include <arch/x86_64/system/cpu.hpp>

module system.cpu.arch;

import system.cpu.self;
import lib;

namespace cpu
{
    namespace mtrr
    {
        // https://github.com/limine-bootloader/limine/blob/7952e9c79a1ee9e6536b366bf8fc42a662b3ba92/common/mm/mtrr.c

        namespace
        {
            std::uint64_t *saved_mtrrs = nullptr;
        } // namespace

        void save()
        {
            if (!supported())
                return;

            const auto ia32_mtrrcap = msr::read(0xFE);
            const std::uint8_t var_reg_count = ia32_mtrrcap & 0xFF;

            if (!saved_mtrrs)
                saved_mtrrs = new std::uint64_t[(var_reg_count * 2) + 12];

            for (std::size_t i = 0; i < var_reg_count * 2; i += 2)
            {
                saved_mtrrs[i] = msr::read(0x200 + i);
                saved_mtrrs[i + 1] = msr::read(0x200 + i + 1);
            }

            // fixed range MTRRs
            saved_mtrrs[var_reg_count * 2 + 0] = msr::read(0x250);
            saved_mtrrs[var_reg_count * 2 + 1] = msr::read(0x258);
            saved_mtrrs[var_reg_count * 2 + 2] = msr::read(0x259);
            saved_mtrrs[var_reg_count * 2 + 3] = msr::read(0x268);
            saved_mtrrs[var_reg_count * 2 + 4] = msr::read(0x269);
            saved_mtrrs[var_reg_count * 2 + 5] = msr::read(0x26A);
            saved_mtrrs[var_reg_count * 2 + 6] = msr::read(0x26B);
            saved_mtrrs[var_reg_count * 2 + 7] = msr::read(0x26C);
            saved_mtrrs[var_reg_count * 2 + 8] = msr::read(0x26D);
            saved_mtrrs[var_reg_count * 2 + 9] = msr::read(0x26E);
            saved_mtrrs[var_reg_count * 2 + 10] = msr::read(0x26F);

            // MTRR default type
            saved_mtrrs[var_reg_count * 2 + 11] = msr::read(0x2FF);
            saved_mtrrs[var_reg_count * 2 + 11] &= ~(1ul << 11);
        }

        void restore()
        {
            if (!supported() || saved_mtrrs == nullptr)
                return;

            const auto ia32_mtrrcap = msr::read(0xFE);
            const std::uint8_t var_reg_count = ia32_mtrrcap & 0xFF;

            // save old cr0 and then enable the CD flag and disable the NW flag
            std::uintptr_t old_cr0;
            asm volatile ("mov %0, cr0" : "=r"(old_cr0) :: "memory");
            std::uintptr_t new_cr0 = (old_cr0 | (1 << 30)) & ~(1ul << 29);
            asm volatile ("mov cr0, %0" :: "r"(new_cr0) : "memory");

            // then invalidate the caches
            asm volatile ("wbinvd" ::: "memory");

            // do a cr3 read/write to flush the TLB
            std::uintptr_t cr3;
            asm volatile ("mov %0, cr3" : "=r"(cr3) :: "memory");
            asm volatile ("mov cr3, %0" :: "r"(cr3) : "memory");

            // disable the MTRRs
            auto mtrr_def = msr::read(0x2ff);
            mtrr_def &= ~(1ul << 11);
            msr::write(0x2ff, mtrr_def);

            // restore variable range MTRRs
            for (std::size_t i = 0; i < var_reg_count * 2; i += 2)
            {
                msr::write(0x200 + i, saved_mtrrs[i]);
                msr::write(0x200 + i + 1, saved_mtrrs[i + 1]);
            }

            // restore fixed range MTRRs
            msr::write(0x250, saved_mtrrs[var_reg_count * 2 + 0]);
            msr::write(0x258, saved_mtrrs[var_reg_count * 2 + 1]);
            msr::write(0x259, saved_mtrrs[var_reg_count * 2 + 2]);
            msr::write(0x268, saved_mtrrs[var_reg_count * 2 + 3]);
            msr::write(0x269, saved_mtrrs[var_reg_count * 2 + 4]);
            msr::write(0x26A, saved_mtrrs[var_reg_count * 2 + 5]);
            msr::write(0x26B, saved_mtrrs[var_reg_count * 2 + 6]);
            msr::write(0x26C, saved_mtrrs[var_reg_count * 2 + 7]);
            msr::write(0x26D, saved_mtrrs[var_reg_count * 2 + 8]);
            msr::write(0x26E, saved_mtrrs[var_reg_count * 2 + 9]);
            msr::write(0x26F, saved_mtrrs[var_reg_count * 2 + 10]);

            // restore MTRR default type
            msr::write(0x2FF, saved_mtrrs[var_reg_count * 2 + 11]);

            // now do the opposite of the cache disable and flush from above

            // re-enable MTRRs
            mtrr_def = msr::read(0x2FF);
            mtrr_def |= (1 << 11);
            msr::write(0x2FF, mtrr_def);

            // do a cr3 read/write to flush the TLB
            asm volatile ("mov %0, cr3" : "=r"(cr3) :: "memory");
            asm volatile ("mov cr3, %0" :: "r"(cr3) : "memory");

            // then invalidate the caches
            asm volatile ("wbinvd" ::: "memory");

            // restore old value of cr0
            asm volatile ("mov cr0, %0" :: "r"(old_cr0) : "memory");
        }
    } // namespace mtrr

    namespace features
    {
        namespace
        {
            constexpr std::uint64_t rfbm = ~0ull;
            constexpr std::uint32_t rfbm_low = rfbm & 0xFFFFFFFF;
            constexpr std::uint32_t rfbm_high = (rfbm >> 32) & 0xFFFFFFFF;

            cpu_local<fpu> fpu_percpu;
            cpu_local_init(fpu_percpu);
        } // namespace

        fpu &get_fpu()
        {
            return fpu_percpu.get();
        }

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
            // // clear MISC_ENABLE.LCMV
            // // static const auto cached0 = (cpu::id(0, 0)->a > 2 && (cpu::id(0x07, 1)->b & (1 << 3)) == 0);
            // // if (cached0)
            //     msr::write(0x1A0, msr::read(0x1A0) & ~(1 << 22));

            // SSE
            {
                auto cr0 = rdreg(cr0);
                cr0 = (cr0 & ~(1 << 2)) | (1 << 1);
                wrreg(cr0, cr0);

                auto cr4 = rdreg(cr4);
                cr4 |= (1 << 9) | (1 << 10);
                wrreg(cr4, cr4);
            }

            cpu::id_res res1;
            const auto cached1 = cpu::id(0x01, 0, res1);

            cpu::id_res res7;
            const auto cached7 = cpu::id(0x07, 0, res7);

            cpu::id_res res13;
            if (!cpu::id(0x0D, 0, res13))
                lib::panic("cpu::id(0x0D, 0) failed");

            // UMIP SMEP SMAP INVPCID
            if (cached7)
            {
                auto cr4 = rdreg(cr4);
                {
                    if (res7.c & (1 << 2))
                        cr4 |= (1 << 11);

                    if (res7.b & (1 << 7))
                        cr4 |= (1 << 20);

                    if (res7.b & (1 << 20))
                    {
                        cr4 |= (1 << 21);
                        smap::supported = true;
                    }

                    if ((res1.c & (1 << 17)) && res7.b & (1 << 10))
                    {
                        cr4 |= (1 << 17);
                        has_pcids = true;
                    }
                }
                wrreg(cr4, cr4);
            }

            // // TSC in ring 0
            // {
            //     auto cr4 = rdreg(cr4);
            //     cr4 |= (1 << 2);
            //     wrreg(cr4, cr4);
            // }

            if (cached1 && (res1.c & (1 << 26)))
            {
                // xsave
                wrreg(cr4, rdreg(cr4) | (1 << 18));

                // x87 and SSE
                std::uint64_t xcr0 = 0b11;
                // AVX
                if (res1.c & (1 << 28))
                    xcr0 |= (1 << 2);
                // AVX512
                if (cached7 && (res7.b & (1 << 16)))
                    xcr0 |= (0b111 << 5);

                asm volatile ("xsetbv" :: "a"(xcr0), "d"(xcr0 >> 32), "c"(0) : "memory");

                bool xopt = cpu::id(0x0D, 1).value_or(cpu::id_res { }).a & (1 << 0);

                auto &fpu = get_fpu();
                fpu.size = res13.c;
                fpu.save = xopt ? xsaveopt : xsave;
                fpu.restore = xrstor;
            }
            else
            {
                auto &fpu = get_fpu();
                fpu.size = 512;
                fpu.save = fxsave;
                fpu.restore = fxrstor;
            }
        }
    } // namespace features
} // export namespace cpu