-- Copyright (C) 2024  ilobilo

package("compiler-rt-builtins")
    add_urls("https://github.com/ilobilo/compiler-rt-builtins.git")
    add_versions("latest", "master")

    add_deps("freestnd-c-hdrs")

    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_requires("freestnd-c-hdrs")
            target("compiler-rt-builtins")
                add_packages("freestnd-c-hdrs")

                set_kind("static")
                set_languages("c17")

                add_cflags("-fno-builtin")
                add_cflags("-fvisibility=hidden")
                add_cflags("-fomit-frame-pointer")
                add_cflags("-ffreestanding")
                add_cflags("-Wno-pedantic")
                add_cflags("-Wno-undef")
                add_cflags("-Wno-sign-conversion")
                add_cflags("-Wno-double-promotion")
                -- add_cflags("-Wno-float-equal")
                -- add_cflags("-Wno-float-conversion")
                add_cflags("-Wno-conversion")
                add_cflags("-Wno-missing-noreturn")
                add_cflags("-Wno-unused-parameter")
                add_cflags("-Wno-format-nonliteral")
                add_cflags("-Wno-unused-macros")

                add_cflags("-Wno-builtin-declaration-mismatch")
                add_cflags("-Wno-shift-sign-overflow")
                add_cflags("-Wno-shorten-64-to-32")
                add_cflags("-Wno-unreachable-code-break")
                add_cflags("-Wno-conditional-uninitialized")
                add_cflags("-Wno-missing-variable-declarations")
                add_cflags("-Wno-reserved-id-macro")
                add_cflags("-Wno-missing-prototypes")

                add_defines("__ELF__")

                local generic_sources = {
                    "builtins/absvdi2.c",
                    "builtins/absvsi2.c",
                    "builtins/absvti2.c",
                    -- "builtins/adddf3.c",
                    -- "builtins/addsf3.c",
                    -- "builtins/addtf3.c",
                    "builtins/addvdi3.c",
                    "builtins/addvsi3.c",
                    "builtins/addvti3.c",
                    "builtins/apple_versioning.c",
                    "builtins/ashldi3.c",
                    "builtins/ashlti3.c",
                    "builtins/ashrdi3.c",
                    "builtins/ashrti3.c",
                    "builtins/clear_cache.c",
                    "builtins/clzti2.c",
                    "builtins/cmpdi2.c",
                    "builtins/cmpti2.c",
                    -- "builtins/comparedf2.c",
                    "builtins/ctzdi2.c",
                    "builtins/ctzsi2.c",
                    "builtins/ctzti2.c",
                    -- "builtins/divdc3.c",
                    -- "builtins/divdf3.c",
                    "builtins/divdi3.c",
                    "builtins/divmoddi4.c",
                    -- "builtins/divsc3.c",
                    -- "builtins/divsf3.c",
                    -- "builtins/divtc3.c",
                    "builtins/divti3.c",
                    -- "builtins/divtf3.c",
                    -- "builtins/extendsfdf2.c",
                    -- "builtins/extendhfsf2.c",
                    -- "builtins/ffsdi2.c",
                    -- "builtins/ffssi2.c",
                    -- "builtins/ffsti2.c",
                    -- "builtins/fixdfdi.c",
                    -- "builtins/fixdfsi.c",
                    -- "builtins/fixdfti.c",
                    -- "builtins/fixsfdi.c",
                    -- "builtins/fixsfsi.c",
                    -- "builtins/fixsfti.c",
                    -- "builtins/fixunsdfdi.c",
                    -- "builtins/fixunsdfsi.c",
                    -- "builtins/fixunsdfti.c",
                    -- "builtins/fixunssfdi.c",
                    -- "builtins/fixunssfsi.c",
                    -- "builtins/fixunssfti.c",
                    -- "builtins/floatsidf.c",
                    -- "builtins/floatsisf.c",
                    -- "builtins/floattidf.c",
                    -- "builtins/floattisf.c",
                    -- "builtins/floatunsidf.c",
                    -- "builtins/floatunsisf.c",
                    -- "builtins/floatuntidf.c",
                    -- "builtins/floatuntisf.c",
                    "builtins/int_util.c",
                    "builtins/lshrdi3.c",
                    "builtins/lshrti3.c",
                    "builtins/moddi3.c",
                    "builtins/modti3.c",
                    -- "builtins/muldc3.c",
                    -- "builtins/muldf3.c",
                    "builtins/muldi3.c",
                    "builtins/mulodi4.c",
                    "builtins/mulosi4.c",
                    "builtins/muloti4.c",
                    -- "builtins/mulsc3.c",
                    -- "builtins/mulsf3.c",
                    "builtins/multi3.c",
                    -- "builtins/multf3.c",
                    "builtins/mulvdi3.c",
                    "builtins/mulvsi3.c",
                    "builtins/mulvti3.c",
                    -- "builtins/negdf2.c",
                    "builtins/negdi2.c",
                    -- "builtins/negsf2.c",
                    "builtins/negti2.c",
                    "builtins/negvdi2.c",
                    "builtins/negvsi2.c",
                    "builtins/negvti2.c",
                    -- This file is used by Obj-C and won"t build without clang
                    -- "builtins/os_version_check.c",
                    "builtins/paritydi2.c",
                    "builtins/paritysi2.c",
                    "builtins/parityti2.c",
                    "builtins/popcountdi2.c",
                    "builtins/popcountsi2.c",
                    "builtins/popcountti2.c",
                    -- "builtins/powidf2.c",
                    -- "builtins/powisf2.c",
                    -- "builtins/powitf2.c",
                    -- "builtins/subdf3.c",
                    -- "builtins/subsf3.c",
                    "builtins/subvdi3.c",
                    "builtins/subvsi3.c",
                    "builtins/subvti3.c",
                    -- "builtins/subtf3.c",
                    "builtins/trampoline_setup.c",
                    -- "builtins/truncdfhf2.c",
                    -- "builtins/truncdfsf2.c",
                    -- "builtins/truncsfhf2.c",
                    "builtins/ucmpdi2.c",
                    "builtins/ucmpti2.c",
                    "builtins/udivdi3.c",
                    "builtins/udivmoddi4.c",
                    "builtins/udivmodti4.c",
                    "builtins/udivti3.c",
                    "builtins/umoddi3.c",
                    "builtins/umodti3.c"
                }

                local generic_nonarm_sources = {
                    "builtins/bswapdi2.c",
                    "builtins/bswapsi2.c",
                    "builtins/clzdi2.c",
                    "builtins/clzsi2.c",
                    -- "builtins/comparesf2.c",
                    "builtins/divmodsi4.c",
                    "builtins/divsi3.c",
                    "builtins/modsi3.c",
                    "builtins/udivmodsi4.c",
                    "builtins/udivsi3.c",
                    "builtins/umodsi3.c"
                }

                local generic_nonx86_64_sources = {
                    -- "builtins/floatdidf.c",
                    -- "builtins/floatdisf.c",
                    -- "builtins/floatundidf.c",
                    -- "builtins/floatundisf.c"
                }

                local generic_tf_sources = {
                    -- "builtins/comparetf2.c",
                    -- "builtins/extenddftf2.c",
                    -- "builtins/extendsftf2.c",
                    -- "builtins/fixtfdi.c",
                    -- "builtins/fixtfsi.c",
                    -- "builtins/fixtfti.c",
                    -- "builtins/fixunstfdi.c",
                    -- "builtins/fixunstfsi.c",
                    -- "builtins/fixunstfti.c",
                    -- "builtins/floatditf.c",
                    -- "builtins/floatsitf.c",
                    -- "builtins/floattitf.c",
                    -- "builtins/floatunditf.c",
                    -- "builtins/floatunsitf.c",
                    -- "builtins/floatuntitf.c",
                    -- "builtins/multc3.c",
                    -- "builtins/trunctfdf2.c",
                    -- "builtins/trunctfsf2.c"
                }

                local x86_files = {
                    -- "builtins/divxc3.c",
                    -- "builtins/fixxfdi.c",
                    -- "builtins/fixxfti.c",
                    -- "builtins/fixunsxfdi.c",
                    -- "builtins/fixunsxfsi.c",
                    -- "builtins/fixunsxfti.c",
                    -- "builtins/floatdixf.c",
                    -- "builtins/floattixf.c",
                    -- "builtins/floatundixf.c",
                    -- "builtins/floatuntixf.c",
                    -- "builtins/mulxc3.c",
                    -- "builtins/powixf2.c"
                }

                local x86_64_files = {
                    -- "builtins/x86_64/floatdidf.c",
                    -- "builtins/x86_64/floatdisf.c",
                    -- "builtins/x86_64/floatdixf.c",
                    -- "builtins/x86_64/floatundidf.S",
                    -- "builtins/x86_64/floatundisf.S",
                    -- "builtins/x86_64/floatundixf.S"

                    -- "builtins/cpu_model.c"
                }

                local aarch64_files = {
                    -- "builtins/aarch64/fp_mode.c"

                    -- "builtins/cpu_model.c"
                }

                local atomic_files = {
                    "builtins/atomic.c"
                }

                local apple_atomic_files = {
                    "builtins/atomic_flag_clear.c",
                    "builtins/atomic_flag_clear_explicit.c",
                    "builtins/atomic_flag_test_and_set.c",
                    "builtins/atomic_flag_test_and_set_explicit.c",
                    "builtins/atomic_signal_fence.c",
                    "builtins/atomic_thread_fence.c"
                }

                local gcc_unwind_files = {
                    "builtins/gcc_personality_v0.c"
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

                add_includedirs("builtins")
        ]])
        local configs = { }
        import("package.tools.xmake").install(package, configs)
    end)