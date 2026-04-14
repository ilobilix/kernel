// Copyright (C) 2024-2025  ilobilo

module;

#include <magic_enum.hpp>
#include <magic_enum_flags.hpp>
#include <magic_enum_format.hpp>
#include <magic_enum_fuse.hpp>
#include <magic_enum_switch.hpp>
#include <magic_enum_utility.hpp>

export module magic_enum;

export namespace magic_enum
{
    using ::magic_enum::enum_cast;
    using ::magic_enum::enum_value;
    using ::magic_enum::enum_values;
    using ::magic_enum::enum_count;
    using ::magic_enum::enum_integer;
    using ::magic_enum::enum_name;
    using ::magic_enum::enum_names;
    using ::magic_enum::enum_entries;
    using ::magic_enum::enum_index;
    using ::magic_enum::enum_contains;
    using ::magic_enum::enum_reflected;
    using ::magic_enum::enum_type_name;
    using ::magic_enum::enum_fuse;
    using ::magic_enum::enum_switch;
    using ::magic_enum::enum_for_each;

    using ::magic_enum::enum_flags_name;
    using ::magic_enum::enum_flags_cast;
    using ::magic_enum::enum_flags_contains;
    using ::magic_enum::enum_flags_test;
    using ::magic_enum::enum_flags_test_any;

    using ::magic_enum::is_unscoped_enum;
    using ::magic_enum::is_unscoped_enum_v;
    using ::magic_enum::is_scoped_enum;
    using ::magic_enum::is_scoped_enum_v;

    using ::magic_enum::underlying_type;
    using ::magic_enum::underlying_type_t;

    namespace bitwise_operators
    {
        using ::magic_enum::bitwise_operators::operator&;
        using ::magic_enum::bitwise_operators::operator^;
        using ::magic_enum::bitwise_operators::operator|;
        using ::magic_enum::bitwise_operators::operator~;
        using ::magic_enum::bitwise_operators::operator&=;
        using ::magic_enum::bitwise_operators::operator^=;
        using ::magic_enum::bitwise_operators::operator|=;
    } // namespace bitwise_operators
} // export namespace magic_enum