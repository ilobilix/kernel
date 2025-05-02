// Copyright (C) 2024-2025  ilobilo

module;

#include <cassert>

export module x86_64.system.cpu;
import cppstd;

export namespace cpu
{
    #define rdreg(reg)                                             \
    ({                                                             \
        std::uintptr_t val;                                        \
        asm volatile ("mov %0, " #reg "" : "=r"(val) :: "memory"); \
        val;                                                       \
    })

    #define wrreg(reg, val) asm volatile ("mov " #reg ", %0" :: "r"(val) : "memory")

    struct registers
    {
        std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        std::uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
        std::uint64_t vector, error_code, rip, cs, rflags, rsp, ss;

        std::uintptr_t fp() { return rbp; }
        std::uintptr_t ip() { return rip; }
    };

    struct extra_regs
    {
        std::uint64_t cr2, cr3, cr4;

        static extra_regs read()
        {
            return { rdreg(cr2), rdreg(cr3), rdreg(cr4) };
        }
    };

    bool id(std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t &eax, std::uint32_t &ebx, std::uint32_t &ecx, std::uint32_t &edx, bool check_max = true)
    {
        static const auto cached = [leaf]
        {
            std::uint32_t cpuid_max = 0;
            asm volatile ("cpuid" : "=a"(cpuid_max) : "a"(leaf & 0x80000000) : "ebx", "ecx", "edx");
            if (leaf > cpuid_max)
                return false;
            return true;
        } ();

        if (check_max && !cached)
            return false;

        asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(subleaf));
        return true;
    }

    bool in_hypervisor()
    {
        static const auto cached = [] -> bool
        {
            std::uint32_t a, b, c, d;
            return cpu::id(1, 0, a, b, c, d) && (c & (1 << 31));
        } ();
        return cached;
    }

    std::uint32_t kvm_base()
    {
        static const auto cached = []
        {
            if (in_hypervisor())
            {
                std::uint32_t eax = 0, signature[3] { };
                for (std::uint32_t base = 0x40000000; base < 0x40010000; base += 0x100)
                {
                    cpu::id(base, 0, eax, signature[0], signature[1], signature[2], false);
                    if (!std::memcmp("KVMKVMKVM\0\0\0", signature, 12))
                        return base;
                }
            }
            return 0u;
        } ();
        return cached;
    }

    void invlpg(std::uintptr_t addr)
    {
        asm volatile ("invlpg [%0]" :: "r"(addr) : "memory");
    }

    namespace msr
    {
        std::uint64_t read(std::uint32_t msr)
        {
            std::uint32_t edx, eax;
            asm volatile ("rdmsr" : "=a"(eax), "=d"(edx) : "c"(msr) : "memory");
            return (static_cast<std::uint64_t>(edx) << 32) | eax;
        }

        void write(std::uint32_t msr, std::uint64_t value)
        {
            std::uint32_t edx = value >> 32;
            auto eax = static_cast<std::uint32_t>(value);
            asm volatile ("wrmsr" :: "a"(eax), "d"(edx), "c"(msr) : "memory");
        }
    } // namespace msr

    namespace smap
    {
        inline void enable() { asm volatile ("stac" ::: "cc"); }
        inline void disable() { asm volatile ("clac" ::: "cc"); }

        struct guard
        {
            bool can_smap;

            guard();
            ~guard();
        };

        template<typename Func, typename ...Args>
        auto as_user(Func &&func, Args &&...args) -> std::invoke_result_t<Func, Args...>
        {
            guard _ { };
            return func(std::forward<Args>(args)...);
        }

        template<typename Func, typename ...Args>
            requires (std::same_as<std::invoke_result_t<Func, Args...>, void>)
        auto as_user(Func &&func, Args &&...args) -> void
        {
            guard _ { };
            func(std::forward<Args>(args)...);
        }
    } // namespace smap

    namespace pat
    {
        enum entries : std::uint64_t
        {
            uncacheable_strong = 0x00,
            write_combining = 0x01,
            write_through = 0x04,
            write_protected = 0x05,
            write_back = 0x06,
            uncacheable = 0x07
        };

        constexpr std::uint64_t boot_state =
            (write_combining << 40) |
            (write_protected << 32) |
            (uncacheable_strong << 24) |
            (uncacheable << 16) |
            (write_through << 8) |
            (write_back);

        bool supported()
        {
            static const auto cached = [] -> bool
            {
                std::uint32_t a, b, c, d;
                return cpu::id(1, 0, a, b, c, d) && (d & (1 << 8));
            } ();
            return cached;
        }

        void write(std::uint64_t value)
        {
            msr::write(0x277, value);
        }
    } // namespace pat

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

        using fpu_func = void (*)(std::byte *);
        std::pair<bool, std::tuple<std::size_t, fpu_func, fpu_func>> enable()
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

            // UMIP SMEP SMAP
            bool can_smap = false;
            {
                static std::uint32_t a, b, c, d;
                static const auto cached = [] { return cpu::id(0x07, 0, a, b, c, d); } ();

                if (cached)
                {
                    auto cr4 = rdreg(cr4);
                    {
                        if (c & (1 << 2))
                            cr4 |= (1 << 11);

                        if (b & (1 << 7))
                            cr4 |= (1 << 20);

                        if (b & (1 << 20))
                        {
                            cr4 |= (1 << 21);
                            can_smap = true;
                        }
                    }
                    wrreg(cr4, cr4);
                }
            }

            // TSC
            {
                auto cr4 = rdreg(cr4);
                cr4 |= (1 << 2);
                wrreg(cr4, cr4);
            }

            std::uint32_t a, b, c, d;
            if (cpu::id(0x01, 0, a, b, c, d) && (c & (1 << 26)))
            {
                // xsave
                wrreg(cr4, rdreg(cr4) | (1 << 18));

                // x87 and SSE
                std::uint64_t xcr0 = 0b11;
                // AVX
                if (c & (1 << 28))
                    xcr0 |= (1 << 2);
                // AVX512
                if (cpu::id(0x07, 0, a, b, c, d) && (b & (1 << 16)))
                    xcr0 |= (0b111 << 5);

                asm volatile ("xsetbv" :: "a"(xcr0), "d"(xcr0 >> 32), "c"(0) : "memory");

                assert(cpu::id(0x0D, 0, a, b, c, d));

                bool xopt = cpu::id(0x0D, 1, a, b, c, d) && (a & (1 << 0));
                return { can_smap, { c, xopt ? xsaveopt : xsave, xrstor } };
            }
            else return { can_smap, { 512, fxsave, fxrstor } };
        }
    } // namespace features

    namespace gs
    {
        std::uintptr_t read()
        {
            std::uintptr_t addr;
            asm volatile ("mov %0, gs:[0]" : "=r"(addr) :: "memory");
            return addr;
        }

        void write_kernel(std::uintptr_t addr)
        {
            msr::write(0xC0000102, addr);
        }

        std::uintptr_t read_kernel()
        {
            return msr::read(0xC0000102);
        }

        void write_user(std::uintptr_t addr)
        {
            msr::write(0xC0000101, addr);
        }

        std::uintptr_t read_user()
        {
            return msr::read(0xC0000101);
        }
    } // namespace gs

    namespace fs
    {
        void write(std::uintptr_t addr)
        {
            msr::write(0xC0000100, addr);
        }

        std::uintptr_t read()
        {
            return msr::read(0xC0000100);
        }
    } // namespace gs

    std::uintptr_t self_addr() { return gs::read(); }
} // export namespace cpu