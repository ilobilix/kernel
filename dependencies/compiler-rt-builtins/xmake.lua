-- Copyright (C) 2024-2025  ilobilo

target("compiler-rt-builtins")
    set_toolchains("ilobilix-clang")
    add_deps("freestnd-c-hdrs")

    set_kind("static")
    set_languages("c17")

    -- add_cflags("-fno-builtin")
    -- add_cflags("-fvisibility=hidden")
    -- add_cflags("-fomit-frame-pointer")
    -- add_cflags("-ffreestanding")
    -- add_cflags("-Wno-pedantic")
    -- add_cflags("-Wno-undef")
    -- add_cflags("-Wno-sign-conversion")
    -- add_cflags("-Wno-double-promotion")
    -- -- add_cflags("-Wno-float-equal")
    -- -- add_cflags("-Wno-float-conversion")
    -- add_cflags("-Wno-conversion")
    -- add_cflags("-Wno-missing-noreturn")
    -- add_cflags("-Wno-unused-parameter")
    -- add_cflags("-Wno-format-nonliteral")
    -- add_cflags("-Wno-unused-macros")

    -- add_cflags("-Wno-builtin-declaration-mismatch")
    -- add_cflags("-Wno-shift-sign-overflow")
    -- add_cflags("-Wno-shorten-64-to-32")
    -- add_cflags("-Wno-unreachable-code-break")
    -- add_cflags("-Wno-conditional-uninitialized")
    -- add_cflags("-Wno-missing-variable-declarations")
    -- add_cflags("-Wno-reserved-id-macro")
    -- add_cflags("-Wno-missing-prototypes")

    add_defines("__ELF__")

    local generic_sources = {
        "compiler-rt-builtins/builtins/absvdi2.c",
        "compiler-rt-builtins/builtins/absvsi2.c",
        "compiler-rt-builtins/builtins/absvti2.c",
        -- "compiler-rt-builtins/builtins/adddf3.c",
        -- "compiler-rt-builtins/builtins/addsf3.c",
        -- "compiler-rt-builtins/builtins/addtf3.c",
        "compiler-rt-builtins/builtins/addvdi3.c",
        "compiler-rt-builtins/builtins/addvsi3.c",
        "compiler-rt-builtins/builtins/addvti3.c",
        "compiler-rt-builtins/builtins/apple_versioning.c",
        "compiler-rt-builtins/builtins/ashldi3.c",
        "compiler-rt-builtins/builtins/ashlti3.c",
        "compiler-rt-builtins/builtins/ashrdi3.c",
        "compiler-rt-builtins/builtins/ashrti3.c",
        "compiler-rt-builtins/builtins/clear_cache.c",
        "compiler-rt-builtins/builtins/clzti2.c",
        "compiler-rt-builtins/builtins/cmpdi2.c",
        "compiler-rt-builtins/builtins/cmpti2.c",
        -- "compiler-rt-builtins/builtins/comparedf2.c",
        "compiler-rt-builtins/builtins/ctzdi2.c",
        "compiler-rt-builtins/builtins/ctzsi2.c",
        "compiler-rt-builtins/builtins/ctzti2.c",
        -- "compiler-rt-builtins/builtins/divdc3.c",
        -- "compiler-rt-builtins/builtins/divdf3.c",
        "compiler-rt-builtins/builtins/divdi3.c",
        "compiler-rt-builtins/builtins/divmoddi4.c",
        -- "compiler-rt-builtins/builtins/divsc3.c",
        -- "compiler-rt-builtins/builtins/divsf3.c",
        -- "compiler-rt-builtins/builtins/divtc3.c",
        "compiler-rt-builtins/builtins/divti3.c",
        -- "compiler-rt-builtins/builtins/divtf3.c",
        -- "compiler-rt-builtins/builtins/extendsfdf2.c",
        -- "compiler-rt-builtins/builtins/extendhfsf2.c",
        -- "compiler-rt-builtins/builtins/ffsdi2.c",
        -- "compiler-rt-builtins/builtins/ffssi2.c",
        -- "compiler-rt-builtins/builtins/ffsti2.c",
        -- "compiler-rt-builtins/builtins/fixdfdi.c",
        -- "compiler-rt-builtins/builtins/fixdfsi.c",
        -- "compiler-rt-builtins/builtins/fixdfti.c",
        -- "compiler-rt-builtins/builtins/fixsfdi.c",
        -- "compiler-rt-builtins/builtins/fixsfsi.c",
        -- "compiler-rt-builtins/builtins/fixsfti.c",
        -- "compiler-rt-builtins/builtins/fixunsdfdi.c",
        -- "compiler-rt-builtins/builtins/fixunsdfsi.c",
        -- "compiler-rt-builtins/builtins/fixunsdfti.c",
        -- "compiler-rt-builtins/builtins/fixunssfdi.c",
        -- "compiler-rt-builtins/builtins/fixunssfsi.c",
        -- "compiler-rt-builtins/builtins/fixunssfti.c",
        -- "compiler-rt-builtins/builtins/floatsidf.c",
        -- "compiler-rt-builtins/builtins/floatsisf.c",
        -- "compiler-rt-builtins/builtins/floattidf.c",
        -- "compiler-rt-builtins/builtins/floattisf.c",
        -- "compiler-rt-builtins/builtins/floatunsidf.c",
        -- "compiler-rt-builtins/builtins/floatunsisf.c",
        -- "compiler-rt-builtins/builtins/floatuntidf.c",
        -- "compiler-rt-builtins/builtins/floatuntisf.c",
        "compiler-rt-builtins/builtins/int_util.c",
        "compiler-rt-builtins/builtins/lshrdi3.c",
        "compiler-rt-builtins/builtins/lshrti3.c",
        "compiler-rt-builtins/builtins/moddi3.c",
        "compiler-rt-builtins/builtins/modti3.c",
        -- "compiler-rt-builtins/builtins/muldc3.c",
        -- "compiler-rt-builtins/builtins/muldf3.c",
        "compiler-rt-builtins/builtins/muldi3.c",
        "compiler-rt-builtins/builtins/mulodi4.c",
        "compiler-rt-builtins/builtins/mulosi4.c",
        "compiler-rt-builtins/builtins/muloti4.c",
        -- "compiler-rt-builtins/builtins/mulsc3.c",
        -- "compiler-rt-builtins/builtins/mulsf3.c",
        "compiler-rt-builtins/builtins/multi3.c",
        -- "compiler-rt-builtins/builtins/multf3.c",
        "compiler-rt-builtins/builtins/mulvdi3.c",
        "compiler-rt-builtins/builtins/mulvsi3.c",
        "compiler-rt-builtins/builtins/mulvti3.c",
        -- "compiler-rt-builtins/builtins/negdf2.c",
        "compiler-rt-builtins/builtins/negdi2.c",
        -- "compiler-rt-builtins/builtins/negsf2.c",
        "compiler-rt-builtins/builtins/negti2.c",
        "compiler-rt-builtins/builtins/negvdi2.c",
        "compiler-rt-builtins/builtins/negvsi2.c",
        "compiler-rt-builtins/builtins/negvti2.c",
        -- This file is used by Obj-C and won"t build without clang
        -- "compiler-rt-builtins/builtins/os_version_check.c",
        "compiler-rt-builtins/builtins/paritydi2.c",
        "compiler-rt-builtins/builtins/paritysi2.c",
        "compiler-rt-builtins/builtins/parityti2.c",
        "compiler-rt-builtins/builtins/popcountdi2.c",
        "compiler-rt-builtins/builtins/popcountsi2.c",
        "compiler-rt-builtins/builtins/popcountti2.c",
        -- "compiler-rt-builtins/builtins/powidf2.c",
        -- "compiler-rt-builtins/builtins/powisf2.c",
        -- "compiler-rt-builtins/builtins/powitf2.c",
        -- "compiler-rt-builtins/builtins/subdf3.c",
        -- "compiler-rt-builtins/builtins/subsf3.c",
        "compiler-rt-builtins/builtins/subvdi3.c",
        "compiler-rt-builtins/builtins/subvsi3.c",
        "compiler-rt-builtins/builtins/subvti3.c",
        -- "compiler-rt-builtins/builtins/subtf3.c",
        "compiler-rt-builtins/builtins/trampoline_setup.c",
        -- "compiler-rt-builtins/builtins/truncdfhf2.c",
        -- "compiler-rt-builtins/builtins/truncdfsf2.c",
        -- "compiler-rt-builtins/builtins/truncsfhf2.c",
        "compiler-rt-builtins/builtins/ucmpdi2.c",
        "compiler-rt-builtins/builtins/ucmpti2.c",
        "compiler-rt-builtins/builtins/udivdi3.c",
        "compiler-rt-builtins/builtins/udivmoddi4.c",
        "compiler-rt-builtins/builtins/udivmodti4.c",
        "compiler-rt-builtins/builtins/udivti3.c",
        "compiler-rt-builtins/builtins/umoddi3.c",
        "compiler-rt-builtins/builtins/umodti3.c"
    }

    local generic_nonarm_sources = {
        "compiler-rt-builtins/builtins/bswapdi2.c",
        "compiler-rt-builtins/builtins/bswapsi2.c",
        "compiler-rt-builtins/builtins/clzdi2.c",
        "compiler-rt-builtins/builtins/clzsi2.c",
        -- "compiler-rt-builtins/builtins/comparesf2.c",
        "compiler-rt-builtins/builtins/divmodsi4.c",
        "compiler-rt-builtins/builtins/divsi3.c",
        "compiler-rt-builtins/builtins/modsi3.c",
        "compiler-rt-builtins/builtins/udivmodsi4.c",
        "compiler-rt-builtins/builtins/udivsi3.c",
        "compiler-rt-builtins/builtins/umodsi3.c"
    }

    local generic_nonx86_64_sources = {
        -- "compiler-rt-builtins/builtins/floatdidf.c",
        -- "compiler-rt-builtins/builtins/floatdisf.c",
        -- "compiler-rt-builtins/builtins/floatundidf.c",
        -- "compiler-rt-builtins/builtins/floatundisf.c"
    }

    local generic_tf_sources = {
        -- "compiler-rt-builtins/builtins/comparetf2.c",
        -- "compiler-rt-builtins/builtins/extenddftf2.c",
        -- "compiler-rt-builtins/builtins/extendsftf2.c",
        -- "compiler-rt-builtins/builtins/fixtfdi.c",
        -- "compiler-rt-builtins/builtins/fixtfsi.c",
        -- "compiler-rt-builtins/builtins/fixtfti.c",
        -- "compiler-rt-builtins/builtins/fixunstfdi.c",
        -- "compiler-rt-builtins/builtins/fixunstfsi.c",
        -- "compiler-rt-builtins/builtins/fixunstfti.c",
        -- "compiler-rt-builtins/builtins/floatditf.c",
        -- "compiler-rt-builtins/builtins/floatsitf.c",
        -- "compiler-rt-builtins/builtins/floattitf.c",
        -- "compiler-rt-builtins/builtins/floatunditf.c",
        -- "compiler-rt-builtins/builtins/floatunsitf.c",
        -- "compiler-rt-builtins/builtins/floatuntitf.c",
        -- "compiler-rt-builtins/builtins/multc3.c",
        -- "compiler-rt-builtins/builtins/trunctfdf2.c",
        -- "compiler-rt-builtins/builtins/trunctfsf2.c"
    }

    local x86_files = {
        -- "compiler-rt-builtins/builtins/divxc3.c",
        -- "compiler-rt-builtins/builtins/fixxfdi.c",
        -- "compiler-rt-builtins/builtins/fixxfti.c",
        -- "compiler-rt-builtins/builtins/fixunsxfdi.c",
        -- "compiler-rt-builtins/builtins/fixunsxfsi.c",
        -- "compiler-rt-builtins/builtins/fixunsxfti.c",
        -- "compiler-rt-builtins/builtins/floatdixf.c",
        -- "compiler-rt-builtins/builtins/floattixf.c",
        -- "compiler-rt-builtins/builtins/floatundixf.c",
        -- "compiler-rt-builtins/builtins/floatuntixf.c",
        -- "compiler-rt-builtins/builtins/mulxc3.c",
        -- "compiler-rt-builtins/builtins/powixf2.c"
    }

    local x86_64_files = {
        -- "compiler-rt-builtins/builtins/x86_64/floatdidf.c",
        -- "compiler-rt-builtins/builtins/x86_64/floatdisf.c",
        -- "compiler-rt-builtins/builtins/x86_64/floatdixf.c",
        -- "compiler-rt-builtins/builtins/x86_64/floatundidf.S",
        -- "compiler-rt-builtins/builtins/x86_64/floatundisf.S",
        -- "compiler-rt-builtins/builtins/x86_64/floatundixf.S"

        -- "compiler-rt-builtins/builtins/cpu_model.c"
    }

    local aarch64_files = {
        -- "compiler-rt-builtins/builtins/aarch64/fp_mode.c"

        -- "compiler-rt-builtins/builtins/cpu_model.c"
    }

    local atomic_files = {
        "compiler-rt-builtins/builtins/atomic.c"
    }

    local apple_atomic_files = {
        "compiler-rt-builtins/builtins/atomic_flag_clear.c",
        "compiler-rt-builtins/builtins/atomic_flag_clear_explicit.c",
        "compiler-rt-builtins/builtins/atomic_flag_test_and_set.c",
        "compiler-rt-builtins/builtins/atomic_flag_test_and_set_explicit.c",
        "compiler-rt-builtins/builtins/atomic_signal_fence.c",
        "compiler-rt-builtins/builtins/atomic_thread_fence.c"
    }

    local gcc_unwind_files = {
        "compiler-rt-builtins/builtins/gcc_personality_v0.c"
    }

    add_files(unpack(generic_sources))
    -- add_files(unpack(atomic_files))

    if is_arch("x86_64") then
        add_files(unpack(x86_files))
        add_files(unpack(x86_64_files))
        add_files(unpack(generic_nonarm_sources))
    elseif is_arch("aarch64") then
        add_files(unpack(aarch64_files))
        add_files(unpack(generic_tf_sources))
        add_files(unpack(generic_nonx86_64_sources))
        add_files(unpack(generic_nonarm_sources))
    end

    add_includedirs("compiler-rt-builtins/builtins/")