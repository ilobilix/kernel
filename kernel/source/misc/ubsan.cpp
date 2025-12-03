// Copyright (C) 2024-2025  ilobilo

import arch;
import lib;
import std;

struct source_location
{
    const char *file;
    std::uint32_t line;
    std::uint32_t column;
};

struct type_descriptor
{
    std::uint16_t kind;
    std::uint16_t info;
    char name[];
};

struct overflow_data
{
    source_location location;
    type_descriptor *type;
};

struct type_mismatch_data
{
    source_location location;
    type_descriptor *type;
    unsigned long alignment;
    unsigned char type_check_kind;
};

struct nonnull_arg_data
{
    source_location location;
    source_location attr_location;
    int arg_index;
};

struct out_of_bounds_data
{
    source_location location;
    type_descriptor *array_type;
    type_descriptor *index_type;
};

struct shift_out_of_bounds_data
{
    source_location location;
    type_descriptor *lhs_type;
    type_descriptor *rhs_type;
};

struct unreachable_data
{
    source_location location;
};

struct invalid_value_data
{
    source_location location;
    type_descriptor *type;
};

struct alignment_assumption_data
{
    source_location location;
    source_location assumption_location;
    type_descriptor *type;
};

struct invalid_builtin_data
{
    source_location location;
    unsigned char kind;
};

#ifdef ILOBILIX_UBSAN

static void print(auto message, source_location loc)
{
    lib::panic("ubsan failure {} at {}:{}:{}", message, loc.file, loc.line, loc.column);
    // lib::fatal("halting the system");
    // lib::stop_all();
}

extern "C"
{
    void __ubsan_handle_add_overflow(overflow_data *data, void *, void *)
    {
        print("add-overflow", data->location);
    }

    void __ubsan_handle_sub_overflow(overflow_data *data, void *, void *)
    {
        print("sub-overflow", data->location);
    }

    void __ubsan_handle_mul_overflow(overflow_data *data, void *, void *)
    {
        print("mul-overflow", data->location);
    }

    void __ubsan_handle_negate_overflow(overflow_data *data, void *)
    {
        print("negate-overflow", data->location);
    }

    void __ubsan_handle_divrem_overflow(overflow_data *data, void *, void *)
    {
        print("divrem-overflow", data->location);
    }

    void __ubsan_handle_pointer_overflow(overflow_data *data, void *, void *)
    {
        print("pointer-overflow", data->location);
    }

    void __ubsan_handle_type_mismatch(type_mismatch_data *data, void *)
    {
        print("type-mismatch", data->location);
    }

    void __ubsan_handle_type_mismatch_v1(type_mismatch_data *data, void *)
    {
        print("type-mismatch-v1", data->location);
    }

    void __ubsan_handle_function_type_mismatch(type_mismatch_data *data, void *)
    {
        print("function-type-mismatch", data->location);
    }

    void __ubsan_handle_nonnull_arg(nonnull_arg_data *data)
    {
        print("nonnull-arg", data->location);
    }

    void __ubsan_handle_nonnull_return_v1(nonnull_arg_data *data)
    {
        print("nonnull-return-v1", data->location);
    }

    void __ubsan_handle_nonnull_return_v1_abort(nonnull_arg_data *data)
    {
        print("nonnull-return-v1-abort", data->location);
    }

    void __ubsan_handle_out_of_bounds(out_of_bounds_data *data, void *)
    {
        print("out-of-bounds", data->location);
    }

    void __ubsan_handle_shift_out_of_bounds(shift_out_of_bounds_data *data, void *, void *)
    {
        print("shift-out-of-bounds", data->location);
    }

    void __ubsan_handle_builtin_unreachable(unreachable_data *data)
    {
        print("builtin-unreachable", data->location);
    }

    void __ubsan_handle_load_invalid_value(invalid_value_data *data, void *)
    {
        print("load-invalid-value", data->location);
    }

    void __ubsan_handle_alignment_assumption(alignment_assumption_data *data, unsigned long, unsigned long, unsigned long)
    {
        print("alignment-assumption", data->location);
    }

    void __ubsan_handle_invalid_builtin(invalid_builtin_data *data)
    {
        print("invalid-builtin", data->location);
    }
} // extern "C"

#endif