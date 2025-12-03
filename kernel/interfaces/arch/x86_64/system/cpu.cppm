// Copyright (C) 2024-2025  ilobilo

module;

#include <arch/x86_64/system/cpu.hpp>

export module system.cpu.arch:impl;
import std;

export namespace cpu
{
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

    struct id_res
    {
        std::uint32_t a, b, c, d;
        constexpr id_res() : a { 0 }, b { 0 }, c { 0 }, d { 0 } { }
    };

    bool id(std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t &eax, std::uint32_t &ebx, std::uint32_t &ecx, std::uint32_t &edx, bool check_max = true)
    {
        static const auto cached = []
        {
            std::uint32_t cpuid_max = 0;
            asm volatile ("cpuid" : "=a"(cpuid_max) : "a"(0x80000000) : "ebx", "ecx", "edx");
            return cpuid_max;
        } ();

        if (check_max && (cached && leaf > cached))
            return false;

        asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(subleaf));
        return true;
    }

    bool id(std::uint32_t leaf, std::uint32_t subleaf, id_res &res, bool check_max = true)
    {
        return id(leaf, subleaf, res.a, res.b, res.c, res.d, check_max);
    }

    std::optional<id_res> id(std::uint32_t leaf, std::uint32_t subleaf, bool check_max = true)
    {
        id_res res;
        return id(leaf, subleaf, res, check_max) ? std::optional<id_res> { res } : std::nullopt;
    }

    bool in_hypervisor()
    {
        static const auto cached = [] -> bool
        {
            id_res res;
            return cpu::id(1, 0, res) && (res.c & (1 << 31));
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

    bool has_pcids = false;
    void invlasid(std::uintptr_t addr, std::size_t asid)
    {
        if (!has_pcids)
            return invlpg(addr);

        struct {
            std::uint64_t pcid;
            const void *address;
        } descriptor { asid, reinterpret_cast<const void *>(addr) };

        std::uint64_t type = 0;
        asm volatile ("invpcid %0, %1" : : "r"(type), "m"(descriptor) : "memory");
    }

    bool has_asids() { return has_pcids; }

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
        inline void disable() { asm volatile ("stac" ::: "cc"); }
        inline void enable() { asm volatile ("clac" ::: "cc"); }

        // if one core supports smap, others do too
        bool supported = false;
    } // namespace smap

    namespace mtrr
    {
        bool supported()
        {
            static const auto cached = [] -> bool
            {
                id_res res;
                return cpu::id(1, 0, res) && (res.d & (1 << 12));
            } ();
            return cached;
        }

        void save();
        void restore();
    } // namespace mtrr

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
                id_res res;
                return cpu::id(1, 0, res) && (res.d & (1 << 8));
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
        // other cores probably share the same state
        struct fpu
        {
            std::size_t size = 0;
            void (*save)(std::byte *) = nullptr;
            void (*restore)(std::byte *) = nullptr;
        };

        void enable();
        fpu &get_fpu();
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

        bool is_set()
        {
            return read_user() != 0;
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