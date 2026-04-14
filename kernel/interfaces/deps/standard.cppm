// based on https://gcc.gnu.org/bugzilla/attachment.cgi?id=58415

module;

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cctype>
#include <cfloat>
#include <climits>
// #include <cmath>
#include <compare>
#include <concepts>
#include <coroutine>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <expected>
#include <functional>
#include <generator>
#include <initializer_list>
#include <iterator>
#include <list>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <ratio>
#include <source_location>
#include <span>
// #include <stdexcept>
#include <string>
#include <string_view>
// #include <system_error>
#include <tuple>
#include <type_traits>
// #include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <version>

export module cppstd;

export namespace std
{
    namespace ranges
    {
        using std::ranges::in_found_result;
        using std::ranges::in_fun_result;
        using std::ranges::in_in_out_result;
        using std::ranges::in_in_result;
        using std::ranges::in_out_out_result;
        using std::ranges::in_out_result;
        using std::ranges::min_max_result;
    } // namespace ranges

    using std::all_of;
    using std::any_of;
    using std::none_of;

    using std::for_each;
    using std::for_each_n;

    using std::find;
    using std::find_if;
    using std::find_if_not;
    using std::find_end;
    using std::find_first_of;

    using std::adjacent_find;

    using std::count;
    using std::count_if;

    using std::mismatch;
    using std::equal;

    using std::is_permutation;

    using std::search;
    using std::search_n;

    using std::copy;
    using std::copy_n;
    using std::copy_if;
    using std::copy_backward;

    using std::move;
    using std::move_backward;

    using std::swap_ranges;
    using std::iter_swap;
    using std::transform;

    using std::replace;
    using std::replace_if;
    using std::replace_copy;
    using std::replace_copy_if;

    using std::fill;
    using std::fill_n;

    using std::generate;
    using std::generate_n;

    using std::remove;
    using std::remove_if;

    using std::remove_copy;
    using std::remove_copy_if;

    using std::unique;
    using std::unique_copy;

    using std::reverse;
    using std::reverse_copy;

    using std::rotate;
    using std::rotate_copy;

    using std::sample;
    using std::shuffle;
    using std::shift_left;
    using std::shift_right;

    using std::sort;
    using std::stable_sort;
    using std::partial_sort;
    using std::partial_sort_copy;

    using std::is_sorted;
    using std::is_sorted_until;

    using std::nth_element;

    using std::lower_bound;
    using std::upper_bound;
    using std::equal_range;
    using std::binary_search;

    using std::is_partitioned;
    using std::partition;
    // using std::stable_partition;
    using std::partition_copy;
    using std::partition_point;

    using std::merge;
    using std::inplace_merge;

    using std::includes;

    using std::set_union;
    using std::set_intersection;
    using std::set_difference;
    using std::set_symmetric_difference;

    using std::push_heap;
    using std::pop_heap;
    using std::make_heap;
    using std::sort_heap;
    using std::is_heap;
    using std::is_heap_until;

    using std::min;
    using std::max;
    using std::minmax;
    using std::min_element;
    using std::max_element;
    using std::minmax_element;

    using std::clamp;

    using std::lexicographical_compare;
    using std::lexicographical_compare_three_way;

    using std::next_permutation;
    using std::prev_permutation;

    namespace ranges
    {
        using std::ranges::to;

        using std::ranges::all_of;
        using std::ranges::any_of;
        using std::ranges::none_of;

        using std::ranges::for_each;
        using std::ranges::for_each_result;

        using std::ranges::for_each_n;
        using std::ranges::for_each_n_result;

        using std::ranges::find;
        using std::ranges::find_if;
        using std::ranges::find_if_not;
        using std::ranges::find_end;
        using std::ranges::find_first_of;

        using std::ranges::adjacent_find;

        using std::ranges::count;
        using std::ranges::count_if;

        using std::ranges::mismatch;
        using std::ranges::mismatch_result;
        using std::ranges::equal;

        using std::ranges::is_permutation;

        using std::ranges::search;
        using std::ranges::search_n;

        using std::ranges::copy;
        using std::ranges::copy_result;
        using std::ranges::copy_n;
        using std::ranges::copy_n_result;
        using std::ranges::copy_if;
        using std::ranges::copy_if_result;
        using std::ranges::copy_backward;
        using std::ranges::copy_backward_result;

        using std::ranges::move;
        using std::ranges::move_result;
        using std::ranges::move_backward;
        using std::ranges::move_backward_result;

        using std::ranges::swap_ranges;
        using std::ranges::swap_ranges_result;

        using std::ranges::binary_transform_result;
        using std::ranges::transform;
        using std::ranges::unary_transform_result;

        using std::ranges::replace;
        using std::ranges::replace_if;

        using std::ranges::replace_copy;
        using std::ranges::replace_copy_if;
        using std::ranges::replace_copy_if_result;
        using std::ranges::replace_copy_result;

        using std::ranges::fill;
        using std::ranges::fill_n;

        using std::ranges::generate;
        using std::ranges::generate_n;

        using std::ranges::remove;
        using std::ranges::remove_if;

        using std::ranges::remove_copy;
        using std::ranges::remove_copy_if;
        using std::ranges::remove_copy_if_result;
        using std::ranges::remove_copy_result;

        using std::ranges::unique;
        using std::ranges::unique_copy;
        using std::ranges::unique_copy_result;

        using std::ranges::reverse;
        using std::ranges::reverse_copy;
        using std::ranges::reverse_copy_result;

        using std::ranges::rotate;
        using std::ranges::rotate_copy;
        using std::ranges::rotate_copy_result;

        using std::ranges::sample;
        using std::ranges::shuffle;

        using std::ranges::sort;
        using std::ranges::stable_sort;
        using std::ranges::partial_sort;
        using std::ranges::partial_sort_copy;
        using std::ranges::partial_sort_copy_result;

        using std::ranges::is_sorted;
        using std::ranges::is_sorted_until;

        using std::ranges::nth_element;

        using std::ranges::lower_bound;
        using std::ranges::upper_bound;
        using std::ranges::equal_range;
        using std::ranges::binary_search;

        using std::ranges::is_partitioned;
        using std::ranges::partition;
        // using std::ranges::stable_partition;
        using std::ranges::partition_copy;
        using std::ranges::partition_copy_result;
        using std::ranges::partition_point;

        using std::ranges::merge;
        using std::ranges::merge_result;
        using std::ranges::inplace_merge;

        using std::ranges::includes;

        using std::ranges::set_union;
        using std::ranges::set_union_result;
        using std::ranges::set_intersection;
        using std::ranges::set_intersection_result;
        using std::ranges::set_difference;
        using std::ranges::set_difference_result;
        using std::ranges::set_symmetric_difference;
        using std::ranges::set_symmetric_difference_result;

        using std::ranges::push_heap;
        using std::ranges::pop_heap;
        using std::ranges::make_heap;
        using std::ranges::sort_heap;
        using std::ranges::is_heap;
        using std::ranges::is_heap_until;

        using std::ranges::min;
        using std::ranges::max;
        using std::ranges::minmax;
        using std::ranges::minmax_result;
        using std::ranges::min_element;
        using std::ranges::max_element;
        using std::ranges::minmax_element;
        using std::ranges::minmax_element_result;

        using std::ranges::clamp;

        using std::ranges::lexicographical_compare;

        using std::ranges::next_permutation;
        using std::ranges::next_permutation_result;
        using std::ranges::prev_permutation;
        using std::ranges::prev_permutation_result;

        using std::ranges::contains;
        using std::ranges::iota;
    } // namespace ranges

    using std::array;
    using std::get;
    using std::to_array;
    using std::tuple_element;
    using std::tuple_size;

    using std::atomic;
    using std::atomic_bool;
    using std::atomic_char;
    using std::atomic_char16_t;
    using std::atomic_char32_t;
    using std::atomic_char8_t;
    using std::atomic_compare_exchange_strong;
    using std::atomic_compare_exchange_strong_explicit;
    using std::atomic_compare_exchange_weak;
    using std::atomic_compare_exchange_weak_explicit;
    using std::atomic_exchange;
    using std::atomic_exchange_explicit;
    using std::atomic_fetch_add;
    using std::atomic_fetch_add_explicit;
    using std::atomic_fetch_and;
    using std::atomic_fetch_and_explicit;
    using std::atomic_fetch_or;
    using std::atomic_fetch_or_explicit;
    using std::atomic_fetch_sub;
    using std::atomic_fetch_sub_explicit;
    using std::atomic_fetch_xor;
    using std::atomic_fetch_xor_explicit;
    using std::atomic_flag;
    using std::atomic_flag_clear;
    using std::atomic_flag_clear_explicit;
    using std::atomic_flag_test;
    using std::atomic_flag_test_and_set;
    using std::atomic_flag_test_and_set_explicit;
    using std::atomic_flag_test_explicit;
    using std::atomic_init;
    using std::atomic_int;
    using std::atomic_int_fast16_t;
    using std::atomic_int_fast32_t;
    using std::atomic_int_fast64_t;
    using std::atomic_int_fast8_t;
    using std::atomic_int_least16_t;
    using std::atomic_int_least32_t;
    using std::atomic_int_least64_t;
    using std::atomic_int_least8_t;
    using std::atomic_intmax_t;
    using std::atomic_intptr_t;
    using std::atomic_is_lock_free;
    using std::atomic_llong;
    using std::atomic_load;
    using std::atomic_load_explicit;
    using std::atomic_long;
    using std::atomic_ptrdiff_t;
    using std::atomic_schar;
    using std::atomic_short;
    using std::atomic_signal_fence;
    using std::atomic_signed_lock_free;
    using std::atomic_size_t;
    using std::atomic_store;
    using std::atomic_store_explicit;
    using std::atomic_thread_fence;
    using std::atomic_uchar;
    using std::atomic_uint;
    using std::atomic_uint_fast16_t;
    using std::atomic_uint_fast32_t;
    using std::atomic_uint_fast64_t;
    using std::atomic_uint_fast8_t;
    using std::atomic_uint_least16_t;
    using std::atomic_uint_least32_t;
    using std::atomic_uint_least64_t;
    using std::atomic_uint_least8_t;
    using std::atomic_uintmax_t;
    using std::atomic_uintptr_t;
    using std::atomic_ullong;
    using std::atomic_ulong;
    using std::atomic_unsigned_lock_free;
    using std::atomic_ushort;
    using std::atomic_wchar_t;
    using std::atomic_ref;
    using std::kill_dependency;
    using std::memory_order;
    using std::memory_order_acq_rel;
    using std::memory_order_acquire;
    using std::memory_order_consume;
    using std::memory_order_relaxed;
    using std::memory_order_release;
    using std::memory_order_seq_cst;

    using std::bit_cast;
    using std::bit_ceil;
    using std::bit_floor;
    using std::bit_width;
    using std::countl_one;
    using std::countl_zero;
    using std::countr_one;
    using std::countr_zero;
    using std::endian;
    using std::has_single_bit;
    using std::popcount;
    using std::rotl;
    using std::rotr;
    using std::byteswap;

    using std::bitset;

    using std::isalnum;
    using std::isalpha;
    using std::isblank;
    using std::iscntrl;
    using std::isdigit;
    using std::isgraph;
    using std::islower;
    using std::isprint;
    using std::ispunct;
    using std::isspace;
    using std::isupper;
    using std::isxdigit;
    using std::tolower;
    using std::toupper;

    using std::common_comparison_category;
    using std::common_comparison_category_t;
    using std::compare_three_way;
    using std::compare_three_way_result;
    using std::compare_three_way_result_t;
    using std::is_eq;
    using std::is_gt;
    using std::is_gteq;
    using std::is_lt;
    using std::is_lteq;
    using std::is_neq;
    using std::partial_ordering;
    using std::strong_ordering;
    using std::three_way_comparable;
    using std::three_way_comparable_with;
    using std::weak_ordering;
    inline namespace _Cpo
    {
        using std::_Cpo::compare_partial_order_fallback;
        using std::_Cpo::compare_strong_order_fallback;
        using std::_Cpo::compare_weak_order_fallback;
        using std::_Cpo::partial_order;
        using std::_Cpo::strong_order;
        using std::_Cpo::weak_order;
    } // namespace _Cpo

    using std::assignable_from;
    using std::common_reference_with;
    using std::common_with;
    using std::convertible_to;
    using std::derived_from;
    using std::floating_point;
    using std::integral;
    using std::same_as;
    using std::signed_integral;
    using std::unsigned_integral;
    namespace ranges
    {
        inline namespace _Cpo
        {
            using std::ranges::_Cpo::swap;
        }
    } // namespace range

    using std::constructible_from;
    using std::copy_constructible;
    using std::copyable;
    using std::default_initializable;
    using std::destructible;
    using std::equality_comparable;
    using std::equality_comparable_with;
    using std::equivalence_relation;
    using std::invocable;
    using std::movable;
    using std::move_constructible;
    using std::predicate;
    using std::regular;
    using std::regular_invocable;
    using std::relation;
    using std::semiregular;
    using std::strict_weak_order;
    using std::swappable;
    using std::swappable_with;
    using std::totally_ordered;
    using std::totally_ordered_with;

    using std::coroutine_handle;
    using std::coroutine_traits;
    using std::noop_coroutine;
    using std::noop_coroutine_handle;
    using std::noop_coroutine_promise;
    using std::suspend_always;
    using std::suspend_never;

    using std::va_list;

    using std::byte;
    using std::max_align_t;
    using std::nullptr_t;
    using std::ptrdiff_t;
    using std::size_t;
    using std::ssize_t;
    using std::to_integer;

    using std::int8_t;
    using std::int16_t;
    using std::int32_t;
    using std::int64_t;
    using std::int_fast16_t;
    using std::int_fast32_t;
    using std::int_fast64_t;
    using std::int_fast8_t;
    using std::int_least16_t;
    using std::int_least32_t;
    using std::int_least64_t;
    using std::int_least8_t;
    using std::intmax_t;
    using std::intptr_t;
    using std::uint8_t;
    using std::uint16_t;
    using std::uint32_t;
    using std::uint64_t;
    using std::uint_fast16_t;
    using std::uint_fast32_t;
    using std::uint_fast64_t;
    using std::uint_fast8_t;
    using std::uint_least16_t;
    using std::uint_least32_t;
    using std::uint_least64_t;
    using std::uint_least8_t;
    using std::uintmax_t;
    using std::uintptr_t;

    using std::fflush;
    using std::FILE;
    using std::fprintf;
    using std::fputc;
    using std::fputs;
    using std::fwrite;
    using std::remove;
    using std::size_t;
    // using std::printf;
    // using std::vprintf;
    // using std::sprintf;
    // using std::vsprintf;
    // using std::snprintf;
    // using std::vsnprintf;
    // using std::asprintf;
    // using std::vasprintf;

    using std::abort;
    using std::atexit;
    using std::atoi;
    using std::atol;
    using std::atoll;
    using std::calloc;
    using std::exit;
    using std::free;
    using std::malloc;
    using std::realloc;
    using std::size_t;
    using std::strtol;
    using std::strtoll;
    using std::strtoul;
    using std::strtoull;

    using ::memchr;
    using std::memcmp;
    using std::memcpy;
    using std::memmove;
    using std::memset;
    using std::size_t;
    using std::strcat;
    using std::strchr;
    using std::strcmp;
    using std::strcpy;
    using std::strlen;
    using std::strnlen;
    using std::strncat;
    using std::strncmp;
    using std::strncpy;
    using std::strstr;

    using std::deque;

    using std::bad_exception;
    using std::current_exception;
    using std::exception;
    namespace __exception_ptr
    {
        using std::__exception_ptr::exception_ptr;
    }
    using std::exception_ptr; // bug: ICE
    using std::get_terminate;
    using std::make_exception_ptr;
    using std::nested_exception;
    using std::rethrow_exception;
    using std::rethrow_if_nested;
    using std::set_terminate;
    using std::terminate;
    using std::terminate_handler;
    using std::throw_with_nested;
    using std::uncaught_exception;
    using std::uncaught_exceptions;

    using std::ranges::enable_borrowed_range;
    using std::ranges::enable_view;

    using std::unexpected;
    using std::bad_expected_access;
    using std::unexpect_t;
    using std::expected;

    using std::bind;
    using std::bind_front;
    using std::bit_and;
    using std::bit_not;
    using std::bit_or;
    using std::bit_xor;
    using std::compare_three_way;
    using std::cref;
    using std::divides;
    using std::equal_to;
    using std::greater;
    using std::greater_equal;
    using std::identity;
    using std::invoke;
    using std::is_bind_expression;
    using std::is_bind_expression_v;
    using std::is_placeholder;
    using std::is_placeholder_v;
    using std::less;
    using std::less_equal;
    using std::logical_and;
    using std::logical_not;
    using std::logical_or;
    using std::minus;
    using std::modulus;
    using std::multiplies;
    using std::negate;
    using std::not_equal_to;
    using std::not_fn;
    using std::plus;
    using std::ref;
    using std::reference_wrapper;
    namespace placeholders
    {
        using std::placeholders::_1;
        using std::placeholders::_10;
        using std::placeholders::_2;
        using std::placeholders::_3;
        using std::placeholders::_4;
        using std::placeholders::_5;
        using std::placeholders::_6;
        using std::placeholders::_7;
        using std::placeholders::_8;
        using std::placeholders::_9;
    } // namespace placeholders
    using std::function;
    using std::mem_fn;
    using std::default_searcher;
    namespace ranges
    {
        using std::ranges::equal_to;
        using std::ranges::greater;
        using std::ranges::greater_equal;
        using std::ranges::less;
        using std::ranges::less_equal;
        using std::ranges::not_equal_to;
    } // namespace ranges

    using std::initializer_list;

    using std::generator;

    using std::incrementable_traits;
    using std::indirectly_readable_traits;
    using std::iter_difference_t;
    using std::iter_reference_t;
    using std::iter_value_t;
    using std::iterator_traits;
    namespace ranges
    {
        inline namespace _Cpo
        {
            using std::ranges::_Cpo::iter_move;
            using std::ranges::_Cpo::iter_swap;
        } // namespace _Cpo
    } // namespace ranges
    using std::advance;
    using std::bidirectional_iterator;
    using std::bidirectional_iterator_tag;
    using std::contiguous_iterator;
    using std::contiguous_iterator_tag;
    using std::disable_sized_sentinel_for;
    using std::distance;
    using std::forward_iterator;
    using std::forward_iterator_tag;
    using std::incrementable;
    using std::indirect_binary_predicate;
    using std::indirect_equivalence_relation;
    using std::indirect_result_t;
    using std::indirect_strict_weak_order;
    using std::indirect_unary_predicate;
    using std::indirectly_comparable;
    using std::indirectly_copyable;
    using std::indirectly_copyable_storable;
    using std::indirectly_movable;
    using std::indirectly_movable_storable;
    using std::indirectly_readable;
    using std::indirectly_regular_unary_invocable;
    using std::indirectly_swappable;
    using std::indirectly_unary_invocable;
    using std::indirectly_writable;
    using std::input_iterator;
    using std::input_iterator_tag;
    using std::input_or_output_iterator;
    using std::iter_common_reference_t;
    using std::iter_rvalue_reference_t;
    using std::mergeable;
    using std::next;
    using std::output_iterator;
    using std::output_iterator_tag;
    using std::permutable;
    using std::prev;
    using std::projected;
    using std::random_access_iterator;
    using std::random_access_iterator_tag;
    using std::sentinel_for;
    using std::sized_sentinel_for;
    using std::sortable;
    using std::weakly_incrementable;
    namespace ranges
    {
        using std::ranges::advance;
        using std::ranges::distance;
        using std::ranges::next;
        using std::ranges::prev;
    } // namespace ranges
    using std::reverse_iterator;
    using std::back_insert_iterator;
    using std::back_inserter;
    using std::begin;
    using std::cbegin;
    using std::cend;
    using std::common_iterator;
    using std::counted_iterator;
    using std::crbegin;
    using std::crend;
    using std::data;
    using std::default_sentinel;
    using std::default_sentinel_t;
    using std::empty;
    using std::end;
    using std::front_insert_iterator;
    using std::front_inserter;
    using std::insert_iterator;
    using std::inserter;
    using std::iterator;
    using std::make_move_iterator;
    using std::make_reverse_iterator;
    using std::move_iterator;
    using std::move_sentinel;
    using std::rbegin;
    using std::rend;
    using std::size;
    using std::ssize;
    using std::unreachable_sentinel;
    using std::unreachable_sentinel_t;
    using std::unreachable;

    using std::float_denorm_style;
    using std::float_round_style;
    using std::numeric_limits;

    using std::list;

    using std::isalnum;
    using std::isalpha;
    using std::isblank;
    using std::iscntrl;
    using std::isdigit;
    using std::isgraph;
    using std::islower;
    using std::isprint;
    using std::ispunct;
    using std::isspace;
    using std::isupper;
    using std::isxdigit;
    using std::tolower;
    using std::toupper;

    using std::align;
    using std::allocator;
    using std::allocator_arg;
    using std::allocator_arg_t;
    using std::allocator_traits;
    using std::assume_aligned;
    using std::make_obj_using_allocator;
    using std::pointer_traits;
    using std::to_address;
    using std::uninitialized_construct_using_allocator;
    using std::uses_allocator;
    using std::uses_allocator_construction_args;
    using std::uses_allocator_v;
    using std::addressof;
    using std::uninitialized_default_construct;
    using std::uninitialized_default_construct_n;
    namespace ranges
    {
        using std::ranges::uninitialized_default_construct;
        using std::ranges::uninitialized_default_construct_n;
    } // namespace ranges
    using std::uninitialized_value_construct;
    using std::uninitialized_value_construct_n;
    namespace ranges
    {
        using std::ranges::uninitialized_value_construct;
        using std::ranges::uninitialized_value_construct_n;
    } // namespace ranges
    using std::uninitialized_copy;
    using std::uninitialized_copy_n;
    namespace ranges
    {
        using std::ranges::uninitialized_copy;
        using std::ranges::uninitialized_copy_n;
        using std::ranges::uninitialized_copy_n_result;
        using std::ranges::uninitialized_copy_result;
    } // namespace ranges
    using std::uninitialized_move;
    using std::uninitialized_move_n;
    namespace ranges
    {
        using std::ranges::uninitialized_move;
        using std::ranges::uninitialized_move_n;
        using std::ranges::uninitialized_move_n_result;
        using std::ranges::uninitialized_move_result;
    } // namespace ranges
    using std::uninitialized_fill;
    using std::uninitialized_fill_n;
    namespace ranges
    {
        using std::ranges::uninitialized_fill;
        using std::ranges::uninitialized_fill_n;
    } // namespace ranges
    using std::construct_at;
    namespace ranges
    {
        using std::ranges::construct_at;
    }
    using std::destroy;
    using std::destroy_at;
    using std::destroy_n;
    namespace ranges
    {
        using std::ranges::destroy;
        using std::ranges::destroy_at;
        using std::ranges::destroy_n;
        using std::ranges::elements_of;
    } // namespace ranges
    using std::default_delete;
    using std::make_unique;
    using std::make_unique_for_overwrite;
    using std::unique_ptr;
    using std::allocate_shared;
    using std::allocate_shared_for_overwrite;
    using std::bad_weak_ptr;
    using std::const_pointer_cast;
    using std::dynamic_pointer_cast;
    using std::make_shared;
    using std::make_shared_for_overwrite;
    using std::reinterpret_pointer_cast;
    using std::shared_ptr;
    using std::static_pointer_cast;
    using std::get_deleter;
    using std::atomic_compare_exchange_strong;
    using std::atomic_compare_exchange_strong_explicit;
    using std::atomic_compare_exchange_weak;
    using std::atomic_compare_exchange_weak_explicit;
    using std::atomic_exchange;
    using std::atomic_exchange_explicit;
    using std::atomic_is_lock_free;
    using std::atomic_load;
    using std::atomic_load_explicit;
    using std::atomic_store;
    using std::atomic_store_explicit;
    using std::enable_shared_from_this;
    using std::owner_less;
    using std::weak_ptr;

    using std::adopt_lock;
    using std::adopt_lock_t;
    using std::defer_lock;
    using std::defer_lock_t;
    using std::lock;
    using std::mutex;
    using std::try_to_lock;
    using std::try_to_lock_t;
    using std::unique_lock;

    using std::align_val_t;
    using std::bad_alloc;
    using std::bad_array_new_length;
    using std::destroying_delete;
    using std::destroying_delete_t;
    using std::get_new_handler;
    using std::launder;
    using std::new_handler;
    using std::nothrow;
    using std::nothrow_t;
    using std::set_new_handler;
} // namespace std

export
{
    // using ::operator new;
    // using ::operator delete;
    using ::operator new[];
    using ::operator delete[];
}

export namespace std::numbers
{
    using std::numbers::e;
    using std::numbers::e_v;
    using std::numbers::egamma;
    using std::numbers::egamma_v;
    using std::numbers::inv_pi;
    using std::numbers::inv_pi_v;
    using std::numbers::inv_sqrt3;
    using std::numbers::inv_sqrt3_v;
    using std::numbers::inv_sqrtpi;
    using std::numbers::inv_sqrtpi_v;
    using std::numbers::ln10;
    using std::numbers::ln10_v;
    using std::numbers::ln2;
    using std::numbers::ln2_v;
    using std::numbers::log10e;
    using std::numbers::log10e_v;
    using std::numbers::log2e;
    using std::numbers::log2e_v;
    using std::numbers::phi;
    using std::numbers::phi_v;
    using std::numbers::pi;
    using std::numbers::pi_v;
    using std::numbers::sqrt2;
    using std::numbers::sqrt2_v;
    using std::numbers::sqrt3;
    using std::numbers::sqrt3_v;
} // namespace std::numbers

export namespace std
{
    using std::accumulate;
    using std::adjacent_difference;
    using std::exclusive_scan;
    using std::inclusive_scan;
    using std::inner_product;
    using std::iota;
    using std::partial_sum;
    using std::reduce;
    using std::transform_exclusive_scan;
    using std::transform_inclusive_scan;
    using std::transform_reduce;
    using std::gcd;
    using std::lcm;
    using std::midpoint;

    using std::bad_optional_access;
    using std::nullopt;
    using std::nullopt_t;
    using std::optional;
    using std::make_optional;

    using std::mersenne_twister_engine;
    using std::mt19937;
    using std::mt19937_64;
    using std::uniform_int_distribution;

    namespace ranges
    {
        inline namespace _Cpo
        {
            using std::ranges::_Cpo::begin;
            using std::ranges::_Cpo::cbegin;
            using std::ranges::_Cpo::cdata;
            using std::ranges::_Cpo::cend;
            using std::ranges::_Cpo::crbegin;
            using std::ranges::_Cpo::crend;
            using std::ranges::_Cpo::data;
            using std::ranges::_Cpo::empty;
            using std::ranges::_Cpo::end;
            using std::ranges::_Cpo::rbegin;
            using std::ranges::_Cpo::rend;
            using std::ranges::_Cpo::size;
            using std::ranges::_Cpo::ssize;
        } // namespace _Cpo
        using std::ranges::bidirectional_range;
        using std::ranges::borrowed_range;
        using std::ranges::common_range;
        using std::ranges::contiguous_range;
        using std::ranges::disable_sized_range;
        using std::ranges::enable_borrowed_range;
        using std::ranges::enable_view;
        using std::ranges::forward_range;
        using std::ranges::get;
        using std::ranges::input_range;
        using std::ranges::iterator_t;
        using std::ranges::output_range;
        using std::ranges::random_access_range;
        using std::ranges::range;
        // using std::ranges::range_common_reference_t; // not-implemented?
        using std::ranges::range_difference_t;
        using std::ranges::range_reference_t;
        using std::ranges::range_rvalue_reference_t;
        using std::ranges::range_size_t;
        using std::ranges::range_value_t;
        using std::ranges::sentinel_t;
        using std::ranges::sized_range;
        using std::ranges::subrange;
        using std::ranges::subrange_kind;
        using std::ranges::view;
        using std::ranges::view_base;
        using std::ranges::view_interface;
        using std::ranges::viewable_range;
    } // namespace ranges
    using std::ranges::get;
    namespace ranges
    {
        using std::ranges::borrowed_iterator_t;
        using std::ranges::borrowed_subrange_t;
        using std::ranges::dangling;
        using std::ranges::empty_view;
        using std::ranges::single_view;
        using std::ranges::iota_view;
        namespace views
        {
            using std::ranges::views::empty;
            using std::ranges::views::single;
            using std::ranges::views::iota;
            using std::ranges::views::repeat;
            using std::ranges::views::all;
            using std::ranges::views::all_t;
        } // namespace views

        using std::ranges::filter_view;
        using std::ranges::owning_view;
        using std::ranges::ref_view;
        using std::ranges::transform_view;
        using std::ranges::take_view;
        using std::ranges::take_while_view;
        using std::ranges::drop_view;
        using std::ranges::drop_while_view;
        using std::ranges::join_view;
        using std::ranges::lazy_split_view;
        using std::ranges::split_view;
        using std::ranges::common_view;
        using std::ranges::reverse_view;
        using std::ranges::elements_view;
        using std::ranges::keys_view;
        using std::ranges::values_view;
        namespace views
        {
            using std::ranges::views::filter;
            using std::ranges::views::enumerate;
            using std::ranges::views::transform;
            using std::ranges::views::take;
            using std::ranges::views::take_while;
            using std::ranges::views::drop;
            using std::ranges::views::drop_while;
            using std::ranges::views::join;
            using std::ranges::views::lazy_split;
            using std::ranges::views::split;
            using std::ranges::views::counted;
            using std::ranges::views::common;
            using std::ranges::views::reverse;
            using std::ranges::views::elements;
            using std::ranges::views::keys;
            using std::ranges::views::values;
        } // namespace views
    } // namespace ranges
    namespace views = ranges::views;
    using std::tuple_element;
    using std::tuple_size;

    using std::atto;
    using std::centi;
    using std::deca;
    using std::deci;
    using std::exa;
    using std::femto;
    using std::giga;
    using std::hecto;
    using std::kilo;
    using std::mega;
    using std::micro;
    using std::milli;
    using std::nano;
    using std::peta;
    using std::pico;
    using std::ratio;
    using std::ratio_add;
    using std::ratio_divide;
    using std::ratio_equal;
    using std::ratio_equal_v;
    using std::ratio_greater;
    using std::ratio_greater_equal;
    using std::ratio_greater_equal_v;
    using std::ratio_greater_v;
    using std::ratio_less;
    using std::ratio_less_equal;
    using std::ratio_less_equal_v;
    using std::ratio_less_v;
    using std::ratio_multiply;
    using std::ratio_not_equal;
    using std::ratio_not_equal_v;
    using std::ratio_subtract;
    using std::tera;

    using std::source_location;

    using std::dynamic_extent;
    using std::span;
    namespace ranges
    {
        using std::ranges::enable_borrowed_range;
        using std::ranges::enable_view;
    } // namespace ranges
    using std::as_bytes;
    using std::as_writable_bytes;

    using std::basic_string;
    using std::char_traits;
    using std::stoi;
    using std::stol;
    using std::stoll;
    using std::stoul;
    using std::stoull;
    using std::string;
    using std::to_string;
    using std::u16string;
    using std::u32string;
    using std::u8string;
    using std::wstring;
    // inline namespace literals
    // {
    //     inline namespace string_literals
    //     {
    //         using std::literals::string_literals::operator""s;
    //     }
    // } // namespace literals

    using std::basic_string_view;
    namespace ranges
    {
        using std::ranges::enable_borrowed_range;
        using std::ranges::enable_view;
    } // namespace ranges
    using std::string_view;
    using std::u16string_view;
    using std::u32string_view;
    using std::u8string_view;
    using std::wstring_view;
    inline namespace literals
    {
        inline namespace string_view_literals
        {
            using std::literals::string_view_literals::operator""sv;
        }
    } // namespace literals

    using std::apply;
    using std::forward_as_tuple;
    using std::get;
    using std::ignore;
    using std::make_from_tuple;
    using std::make_tuple;
    using std::tie;
    using std::tuple;
    using std::_Tuple_impl;
    using std::tuple_cat;
    using std::tuple_element;
    using std::tuple_element_t;
    using std::tuple_size;
    using std::swap;
    using std::tuple_size_v;
    using std::uses_allocator;

    using std::add_const;
    using std::add_const_t;
    using std::add_cv;
    using std::add_cv_t;
    using std::add_lvalue_reference;
    using std::add_lvalue_reference_t;
    using std::add_pointer;
    using std::add_pointer_t;
    using std::add_rvalue_reference;
    using std::add_rvalue_reference_t;
    using std::add_volatile;
    using std::add_volatile_t;
    using std::aligned_storage;
    using std::aligned_storage_t;
    using std::aligned_union;
    using std::aligned_union_t;
    using std::alignment_of;
    using std::alignment_of_v;
    using std::basic_common_reference;
    using std::bool_constant;
    using std::common_reference;
    using std::common_reference_t;
    using std::common_type;
    using std::common_type_t;
    using std::conditional;
    using std::conditional_t;
    using std::conjunction;
    using std::conjunction_v;
    using std::decay;
    using std::decay_t;
    using std::disjunction;
    using std::disjunction_v;
    using std::enable_if;
    using std::enable_if_t;
    using std::extent;
    using std::extent_v;
    using std::false_type;
    using std::has_unique_object_representations;
    using std::has_unique_object_representations_v;
    using std::has_virtual_destructor;
    using std::has_virtual_destructor_v;
    using std::integral_constant;
    using std::invoke_result;
    using std::invoke_result_t;
    using std::is_abstract;
    using std::is_abstract_v;
    using std::is_aggregate;
    using std::is_aggregate_v;
    using std::is_arithmetic;
    using std::is_arithmetic_v;
    using std::is_array;
    using std::is_array_v;
    using std::is_assignable;
    using std::is_assignable_v;
    using std::is_base_of;
    using std::is_base_of_v;
    using std::is_bounded_array;
    using std::is_bounded_array_v;
    using std::is_class;
    using std::is_class_v;
    using std::is_compound;
    using std::is_compound_v;
    using std::is_const;
    using std::is_const_v;
    using std::is_constant_evaluated;
    using std::is_constructible;
    using std::is_constructible_v;
    using std::is_convertible;
    using std::is_convertible_v;
    using std::is_copy_assignable;
    using std::is_copy_assignable_v;
    using std::is_copy_constructible;
    using std::is_copy_constructible_v;
    using std::is_default_constructible;
    using std::is_default_constructible_v;
    using std::is_destructible;
    using std::is_destructible_v;
    using std::is_empty;
    using std::is_empty_v;
    using std::is_enum;
    using std::is_enum_v;
    using std::is_final;
    using std::is_final_v;
    using std::is_floating_point;
    using std::is_floating_point_v;
    using std::is_function;
    using std::is_function_v;
    using std::is_fundamental;
    using std::is_fundamental_v;
    using std::is_integral;
    using std::is_integral_v;
    using std::is_invocable;
    using std::is_invocable_r;
    using std::is_invocable_r_v;
    using std::is_invocable_v;
    using std::is_lvalue_reference;
    using std::is_lvalue_reference_v;
    using std::is_member_function_pointer;
    using std::is_member_function_pointer_v;
    using std::is_member_object_pointer;
    using std::is_member_object_pointer_v;
    using std::is_member_pointer;
    using std::is_member_pointer_v;
    using std::is_move_assignable;
    using std::is_move_assignable_v;
    using std::is_move_constructible;
    using std::is_move_constructible_v;
    using std::is_nothrow_assignable;
    using std::is_nothrow_assignable_v;
    using std::is_nothrow_constructible;
    using std::is_nothrow_constructible_v;
    using std::is_nothrow_convertible;
    using std::is_nothrow_convertible_v;
    using std::is_nothrow_copy_assignable;
    using std::is_nothrow_copy_assignable_v;
    using std::is_nothrow_copy_constructible;
    using std::is_nothrow_copy_constructible_v;
    using std::is_nothrow_default_constructible;
    using std::is_nothrow_default_constructible_v;
    using std::is_nothrow_destructible;
    using std::is_nothrow_destructible_v;
    using std::is_nothrow_invocable;
    using std::is_nothrow_invocable_r;
    using std::is_nothrow_invocable_r_v;
    using std::is_nothrow_invocable_v;
    using std::is_nothrow_move_assignable;
    using std::is_nothrow_move_assignable_v;
    using std::is_nothrow_move_constructible;
    using std::is_nothrow_move_constructible_v;
    using std::is_nothrow_swappable;
    using std::is_nothrow_swappable_v;
    using std::is_nothrow_swappable_with;
    using std::is_nothrow_swappable_with_v;
    using std::is_null_pointer;
    using std::is_null_pointer_v;
    using std::is_object;
    using std::is_object_v;
    using std::is_pod;
    using std::is_pod_v;
    using std::is_pointer;
    using std::is_pointer_v;
    using std::is_polymorphic;
    using std::is_polymorphic_v;
    using std::is_reference;
    using std::is_reference_v;
    using std::is_rvalue_reference;
    using std::is_rvalue_reference_v;
    using std::is_same;
    using std::is_same_v;
    using std::is_scalar;
    using std::is_scalar_v;
    using std::is_signed;
    using std::is_signed_v;
    using std::is_standard_layout;
    using std::is_standard_layout_v;
    using std::is_swappable;
    using std::is_swappable_v;
    using std::is_swappable_with;
    using std::is_swappable_with_v;
    using std::is_trivial;
    using std::is_trivial_v;
    using std::is_trivially_assignable;
    using std::is_trivially_assignable_v;
    using std::is_trivially_constructible;
    using std::is_trivially_constructible_v;
    using std::is_trivially_copy_assignable;
    using std::is_trivially_copy_assignable_v;
    using std::is_trivially_copy_constructible;
    using std::is_trivially_copy_constructible_v;
    using std::is_trivially_copyable;
    using std::is_trivially_copyable_v;
    using std::is_trivially_default_constructible;
    using std::is_trivially_default_constructible_v;
    using std::is_trivially_destructible;
    using std::is_trivially_destructible_v;
    using std::is_trivially_move_assignable;
    using std::is_trivially_move_assignable_v;
    using std::is_trivially_move_constructible;
    using std::is_trivially_move_constructible_v;
    using std::is_unbounded_array;
    using std::is_unbounded_array_v;
    using std::is_union;
    using std::is_union_v;
    using std::is_unsigned;
    using std::is_unsigned_v;
    using std::is_void;
    using std::is_void_v;
    using std::is_volatile;
    using std::is_volatile_v;
    using std::make_signed;
    using std::make_signed_t;
    using std::make_unsigned;
    using std::make_unsigned_t;
    using std::negation;
    using std::negation_v;
    using std::rank;
    using std::rank_v;
    using std::remove_all_extents;
    using std::remove_all_extents_t;
    using std::remove_const;
    using std::remove_const_t;
    using std::remove_cv;
    using std::remove_cv_t;
    using std::remove_cvref;
    using std::remove_cvref_t;
    using std::remove_extent;
    using std::remove_extent_t;
    using std::remove_pointer;
    using std::remove_pointer_t;
    using std::remove_reference;
    using std::remove_reference_t;
    using std::remove_volatile;
    using std::remove_volatile_t;
    using std::true_type;
    using std::type_identity;
    using std::type_identity_t;
    using std::underlying_type;
    using std::underlying_type_t;
    using std::unwrap_ref_decay;
    using std::unwrap_ref_decay_t;
    using std::unwrap_reference;
    using std::unwrap_reference_t;
    using std::void_t;

    using std::bad_cast;
    using std::bad_typeid;
    using std::type_info;

    using std::as_const;
    using std::cmp_equal;
    using std::cmp_greater;
    using std::cmp_greater_equal;
    using std::cmp_less;
    using std::cmp_less_equal;
    using std::cmp_not_equal;
    using std::declval;
    using std::exchange;
    using std::forward;
    using std::in_range;
    using std::index_sequence;
    using std::index_sequence_for;
    using std::integer_sequence;
    using std::make_index_sequence;
    using std::make_integer_sequence;
    using std::move;
    using std::move_if_noexcept;
    using std::pair;
    using std::swap;
    using std::get;
    using std::in_place;
    using std::in_place_index;
    using std::in_place_index_t;
    using std::in_place_t;
    using std::in_place_type;
    using std::in_place_type_t;
    using std::make_pair;
    using std::piecewise_construct;
    using std::piecewise_construct_t;
    using std::tuple_element;
    using std::tuple_size;
    using std::to_underlying;
    namespace rel_ops
    {
        using rel_ops::operator!=;
        using rel_ops::operator>;
        using rel_ops::operator<=;
        using rel_ops::operator>=;
    } // namespace rel_ops

    using std::get;
    using std::get_if;
    using std::holds_alternative;
    using std::variant;
    using std::variant_alternative;
    using std::variant_alternative_t;
    using std::variant_npos;
    using std::variant_size;
    using std::variant_size_v;
    using std::bad_variant_access;
    using std::monostate;
    using std::visit;

    namespace __format
    {
        using std::vector;
    }
    using std::vector;

    using std::erase;
    using std::erase_if;
    using std::swap;
    using std::hash;

    using std::operator+;
    using std::operator==;
    using std::operator!=;
    using std::operator>;
    using std::operator<;
    using std::operator>=;
    using std::operator<=;
    using std::operator<=>;
    using std::operator<<;
    using std::operator>>;

    using std::hardware_constructive_interference_size;
    using std::hardware_destructive_interference_size;
} // namespace std

export
{
    using std::literals::string_view_literals::operator""sv;
} // export