// temporary solution before libstdc++ gets modules support

module;

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
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
#include <format>
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
// #include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <version>

export module std;

#if defined(__clang__)
#  define _USING_IF_EXISTS __attribute__((__using_if_exists__))
#else
#  define _USING_IF_EXISTS
#endif

export namespace std
{
    namespace ranges
    {
        using std::ranges::in_found_result _USING_IF_EXISTS;
        using std::ranges::in_fun_result _USING_IF_EXISTS;
        using std::ranges::in_in_out_result _USING_IF_EXISTS;
        using std::ranges::in_in_result _USING_IF_EXISTS;
        using std::ranges::in_out_out_result _USING_IF_EXISTS;
        using std::ranges::in_out_result _USING_IF_EXISTS;
        using std::ranges::min_max_result _USING_IF_EXISTS;
    } // namespace ranges

    using std::all_of _USING_IF_EXISTS;
    using std::any_of _USING_IF_EXISTS;
    using std::none_of _USING_IF_EXISTS;

    using std::for_each _USING_IF_EXISTS;
    using std::for_each_n _USING_IF_EXISTS;

    using std::find _USING_IF_EXISTS;
    using std::find_if _USING_IF_EXISTS;
    using std::find_if_not _USING_IF_EXISTS;
    using std::find_end _USING_IF_EXISTS;
    using std::find_first_of _USING_IF_EXISTS;

    using std::adjacent_find _USING_IF_EXISTS;

    using std::count _USING_IF_EXISTS;
    using std::count_if _USING_IF_EXISTS;

    using std::mismatch _USING_IF_EXISTS;
    using std::equal _USING_IF_EXISTS;

    using std::is_permutation _USING_IF_EXISTS;

    using std::search _USING_IF_EXISTS;
    using std::search_n _USING_IF_EXISTS;

    using std::copy _USING_IF_EXISTS;
    using std::copy_n _USING_IF_EXISTS;
    using std::copy_if _USING_IF_EXISTS;
    using std::copy_backward _USING_IF_EXISTS;

    using std::move _USING_IF_EXISTS;
    using std::move_backward _USING_IF_EXISTS;

    using std::swap_ranges _USING_IF_EXISTS;
    using std::iter_swap _USING_IF_EXISTS;
    using std::transform _USING_IF_EXISTS;

    using std::replace _USING_IF_EXISTS;
    using std::replace_if _USING_IF_EXISTS;
    using std::replace_copy _USING_IF_EXISTS;
    using std::replace_copy_if _USING_IF_EXISTS;

    using std::fill _USING_IF_EXISTS;
    using std::fill_n _USING_IF_EXISTS;

    using std::generate _USING_IF_EXISTS;
    using std::generate_n _USING_IF_EXISTS;

    using std::remove _USING_IF_EXISTS;
    using std::remove_if _USING_IF_EXISTS;

    using std::remove_copy _USING_IF_EXISTS;
    using std::remove_copy_if _USING_IF_EXISTS;

    using std::unique _USING_IF_EXISTS;
    using std::unique_copy _USING_IF_EXISTS;

    using std::reverse _USING_IF_EXISTS;
    using std::reverse_copy _USING_IF_EXISTS;

    using std::rotate _USING_IF_EXISTS;
    using std::rotate_copy _USING_IF_EXISTS;

    using std::sample _USING_IF_EXISTS;
    using std::shuffle _USING_IF_EXISTS;
    using std::shift_left _USING_IF_EXISTS;
    using std::shift_right _USING_IF_EXISTS;

    using std::sort _USING_IF_EXISTS;
    using std::stable_sort _USING_IF_EXISTS;
    using std::partial_sort _USING_IF_EXISTS;
    using std::partial_sort_copy _USING_IF_EXISTS;

    using std::is_sorted _USING_IF_EXISTS;
    using std::is_sorted_until _USING_IF_EXISTS;

    using std::nth_element _USING_IF_EXISTS;

    using std::lower_bound _USING_IF_EXISTS;
    using std::upper_bound _USING_IF_EXISTS;
    using std::equal_range _USING_IF_EXISTS;
    using std::binary_search _USING_IF_EXISTS;

    using std::is_partitioned _USING_IF_EXISTS;
    using std::partition _USING_IF_EXISTS;
    using std::stable_partition _USING_IF_EXISTS;
    using std::partition_copy _USING_IF_EXISTS;
    using std::partition_point _USING_IF_EXISTS;

    using std::merge _USING_IF_EXISTS;
    using std::inplace_merge _USING_IF_EXISTS;

    using std::includes _USING_IF_EXISTS;

    using std::set_union _USING_IF_EXISTS;
    using std::set_intersection _USING_IF_EXISTS;
    using std::set_difference _USING_IF_EXISTS;
    using std::set_symmetric_difference _USING_IF_EXISTS;

    using std::push_heap _USING_IF_EXISTS;
    using std::pop_heap _USING_IF_EXISTS;
    using std::make_heap _USING_IF_EXISTS;
    using std::sort_heap _USING_IF_EXISTS;
    using std::is_heap _USING_IF_EXISTS;
    using std::is_heap_until _USING_IF_EXISTS;

    using std::min _USING_IF_EXISTS;
    using std::max _USING_IF_EXISTS;
    using std::minmax _USING_IF_EXISTS;
    using std::min_element _USING_IF_EXISTS;
    using std::max_element _USING_IF_EXISTS;
    using std::minmax_element _USING_IF_EXISTS;

    using std::clamp _USING_IF_EXISTS;

    using std::lexicographical_compare _USING_IF_EXISTS;
    using std::lexicographical_compare_three_way _USING_IF_EXISTS;

    using std::next_permutation _USING_IF_EXISTS;
    using std::prev_permutation _USING_IF_EXISTS;

    namespace ranges
    {
        using std::ranges::all_of _USING_IF_EXISTS;
        using std::ranges::any_of _USING_IF_EXISTS;
        using std::ranges::none_of _USING_IF_EXISTS;

        using std::ranges::for_each _USING_IF_EXISTS;
        using std::ranges::for_each_result _USING_IF_EXISTS;

        using std::ranges::for_each_n _USING_IF_EXISTS;
        using std::ranges::for_each_n_result _USING_IF_EXISTS;

        using std::ranges::find _USING_IF_EXISTS;
        using std::ranges::find_if _USING_IF_EXISTS;
        using std::ranges::find_if_not _USING_IF_EXISTS;
        using std::ranges::find_end _USING_IF_EXISTS;
        using std::ranges::find_first_of _USING_IF_EXISTS;

        using std::ranges::adjacent_find _USING_IF_EXISTS;

        using std::ranges::count _USING_IF_EXISTS;
        using std::ranges::count_if _USING_IF_EXISTS;

        using std::ranges::mismatch _USING_IF_EXISTS;
        using std::ranges::mismatch_result _USING_IF_EXISTS;
        using std::ranges::equal _USING_IF_EXISTS;

        using std::ranges::is_permutation _USING_IF_EXISTS;

        using std::ranges::search _USING_IF_EXISTS;
        using std::ranges::search_n _USING_IF_EXISTS;

        using std::ranges::copy _USING_IF_EXISTS;
        using std::ranges::copy_result _USING_IF_EXISTS;
        using std::ranges::copy_n _USING_IF_EXISTS;
        using std::ranges::copy_n_result _USING_IF_EXISTS;
        using std::ranges::copy_if _USING_IF_EXISTS;
        using std::ranges::copy_if_result _USING_IF_EXISTS;
        using std::ranges::copy_backward _USING_IF_EXISTS;
        using std::ranges::copy_backward_result _USING_IF_EXISTS;

        using std::ranges::move _USING_IF_EXISTS;
        using std::ranges::move_result _USING_IF_EXISTS;
        using std::ranges::move_backward _USING_IF_EXISTS;
        using std::ranges::move_backward_result _USING_IF_EXISTS;

        using std::ranges::swap_ranges _USING_IF_EXISTS;
        using std::ranges::swap_ranges_result _USING_IF_EXISTS;

        using std::ranges::binary_transform_result _USING_IF_EXISTS;
        using std::ranges::transform _USING_IF_EXISTS;
        using std::ranges::unary_transform_result _USING_IF_EXISTS;

        using std::ranges::replace _USING_IF_EXISTS;
        using std::ranges::replace_if _USING_IF_EXISTS;

        using std::ranges::replace_copy _USING_IF_EXISTS;
        using std::ranges::replace_copy_if _USING_IF_EXISTS;
        using std::ranges::replace_copy_if_result _USING_IF_EXISTS;
        using std::ranges::replace_copy_result _USING_IF_EXISTS;

        using std::ranges::fill _USING_IF_EXISTS;
        using std::ranges::fill_n _USING_IF_EXISTS;

        using std::ranges::generate _USING_IF_EXISTS;
        using std::ranges::generate_n _USING_IF_EXISTS;

        using std::ranges::remove _USING_IF_EXISTS;
        using std::ranges::remove_if _USING_IF_EXISTS;

        using std::ranges::remove_copy _USING_IF_EXISTS;
        using std::ranges::remove_copy_if _USING_IF_EXISTS;
        using std::ranges::remove_copy_if_result _USING_IF_EXISTS;
        using std::ranges::remove_copy_result _USING_IF_EXISTS;

        using std::ranges::unique _USING_IF_EXISTS;
        using std::ranges::unique_copy _USING_IF_EXISTS;
        using std::ranges::unique_copy_result _USING_IF_EXISTS;

        using std::ranges::reverse _USING_IF_EXISTS;
        using std::ranges::reverse_copy _USING_IF_EXISTS;
        using std::ranges::reverse_copy_result _USING_IF_EXISTS;

        using std::ranges::rotate _USING_IF_EXISTS;
        using std::ranges::rotate_copy _USING_IF_EXISTS;
        using std::ranges::rotate_copy_result _USING_IF_EXISTS;

        using std::ranges::sample _USING_IF_EXISTS;
        using std::ranges::shuffle _USING_IF_EXISTS;

        using std::ranges::sort _USING_IF_EXISTS;
        using std::ranges::stable_sort _USING_IF_EXISTS;
        using std::ranges::partial_sort _USING_IF_EXISTS;
        using std::ranges::partial_sort_copy _USING_IF_EXISTS;
        using std::ranges::partial_sort_copy_result _USING_IF_EXISTS;

        using std::ranges::is_sorted _USING_IF_EXISTS;
        using std::ranges::is_sorted_until _USING_IF_EXISTS;

        using std::ranges::nth_element _USING_IF_EXISTS;

        using std::ranges::lower_bound _USING_IF_EXISTS;
        using std::ranges::upper_bound _USING_IF_EXISTS;
        using std::ranges::equal_range _USING_IF_EXISTS;
        using std::ranges::binary_search _USING_IF_EXISTS;

        using std::ranges::is_partitioned _USING_IF_EXISTS;
        using std::ranges::partition _USING_IF_EXISTS;
        using std::ranges::stable_partition _USING_IF_EXISTS;
        using std::ranges::partition_copy _USING_IF_EXISTS;
        using std::ranges::partition_copy_result _USING_IF_EXISTS;
        using std::ranges::partition_point _USING_IF_EXISTS;

        using std::ranges::merge _USING_IF_EXISTS;
        using std::ranges::merge_result _USING_IF_EXISTS;
        using std::ranges::inplace_merge _USING_IF_EXISTS;

        using std::ranges::includes _USING_IF_EXISTS;

        using std::ranges::set_union _USING_IF_EXISTS;
        using std::ranges::set_union_result _USING_IF_EXISTS;
        using std::ranges::set_intersection _USING_IF_EXISTS;
        using std::ranges::set_intersection_result _USING_IF_EXISTS;
        using std::ranges::set_difference _USING_IF_EXISTS;
        using std::ranges::set_difference_result _USING_IF_EXISTS;
        using std::ranges::set_symmetric_difference _USING_IF_EXISTS;
        using std::ranges::set_symmetric_difference_result _USING_IF_EXISTS;

        using std::ranges::push_heap _USING_IF_EXISTS;
        using std::ranges::pop_heap _USING_IF_EXISTS;
        using std::ranges::make_heap _USING_IF_EXISTS;
        using std::ranges::sort_heap _USING_IF_EXISTS;
        using std::ranges::is_heap _USING_IF_EXISTS;
        using std::ranges::is_heap_until _USING_IF_EXISTS;

        using std::ranges::min _USING_IF_EXISTS;
        using std::ranges::max _USING_IF_EXISTS;
        using std::ranges::minmax _USING_IF_EXISTS;
        using std::ranges::minmax_result _USING_IF_EXISTS;
        using std::ranges::min_element _USING_IF_EXISTS;
        using std::ranges::max_element _USING_IF_EXISTS;
        using std::ranges::minmax_element _USING_IF_EXISTS;
        using std::ranges::minmax_element_result _USING_IF_EXISTS;

        using std::ranges::clamp _USING_IF_EXISTS;

        using std::ranges::lexicographical_compare _USING_IF_EXISTS;

        using std::ranges::next_permutation _USING_IF_EXISTS;
        using std::ranges::next_permutation_result _USING_IF_EXISTS;
        using std::ranges::prev_permutation _USING_IF_EXISTS;
        using std::ranges::prev_permutation_result _USING_IF_EXISTS;
    } // namespace ranges
} // namespace std

export namespace std
{
    using std::any _USING_IF_EXISTS;
    using std::any_cast _USING_IF_EXISTS;
    using std::bad_any_cast _USING_IF_EXISTS;
    using std::make_any _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::array _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::get _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::to_array _USING_IF_EXISTS;
    using std::tuple_element _USING_IF_EXISTS;
    using std::tuple_size _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::atomic _USING_IF_EXISTS;
    using std::atomic_bool _USING_IF_EXISTS;
    using std::atomic_char _USING_IF_EXISTS;
    using std::atomic_char16_t _USING_IF_EXISTS;
    using std::atomic_char32_t _USING_IF_EXISTS;
    using std::atomic_char8_t _USING_IF_EXISTS;
    using std::atomic_compare_exchange_strong _USING_IF_EXISTS;
    using std::atomic_compare_exchange_strong_explicit _USING_IF_EXISTS;
    using std::atomic_compare_exchange_weak _USING_IF_EXISTS;
    using std::atomic_compare_exchange_weak_explicit _USING_IF_EXISTS;
    using std::atomic_exchange _USING_IF_EXISTS;
    using std::atomic_exchange_explicit _USING_IF_EXISTS;
    using std::atomic_fetch_add _USING_IF_EXISTS;
    using std::atomic_fetch_add_explicit _USING_IF_EXISTS;
    using std::atomic_fetch_and _USING_IF_EXISTS;
    using std::atomic_fetch_and_explicit _USING_IF_EXISTS;
    using std::atomic_fetch_or _USING_IF_EXISTS;
    using std::atomic_fetch_or_explicit _USING_IF_EXISTS;
    using std::atomic_fetch_sub _USING_IF_EXISTS;
    using std::atomic_fetch_sub_explicit _USING_IF_EXISTS;
    using std::atomic_fetch_xor _USING_IF_EXISTS;
    using std::atomic_fetch_xor_explicit _USING_IF_EXISTS;
    using std::atomic_flag _USING_IF_EXISTS;
    using std::atomic_flag_clear _USING_IF_EXISTS;
    using std::atomic_flag_clear_explicit _USING_IF_EXISTS;
    using std::atomic_flag_notify_all _USING_IF_EXISTS;
    using std::atomic_flag_notify_one _USING_IF_EXISTS;
    using std::atomic_flag_test _USING_IF_EXISTS;
    using std::atomic_flag_test_and_set _USING_IF_EXISTS;
    using std::atomic_flag_test_and_set_explicit _USING_IF_EXISTS;
    using std::atomic_flag_test_explicit _USING_IF_EXISTS;
    using std::atomic_flag_wait _USING_IF_EXISTS;
    using std::atomic_flag_wait_explicit _USING_IF_EXISTS;
    using std::atomic_init _USING_IF_EXISTS;
    using std::atomic_int _USING_IF_EXISTS;
    using std::atomic_int16_t _USING_IF_EXISTS;
    using std::atomic_int32_t _USING_IF_EXISTS;
    using std::atomic_int64_t _USING_IF_EXISTS;
    using std::atomic_int8_t _USING_IF_EXISTS;
    using std::atomic_int_fast16_t _USING_IF_EXISTS;
    using std::atomic_int_fast32_t _USING_IF_EXISTS;
    using std::atomic_int_fast64_t _USING_IF_EXISTS;
    using std::atomic_int_fast8_t _USING_IF_EXISTS;
    using std::atomic_int_least16_t _USING_IF_EXISTS;
    using std::atomic_int_least32_t _USING_IF_EXISTS;
    using std::atomic_int_least64_t _USING_IF_EXISTS;
    using std::atomic_int_least8_t _USING_IF_EXISTS;
    using std::atomic_intmax_t _USING_IF_EXISTS;
    using std::atomic_intptr_t _USING_IF_EXISTS;
    using std::atomic_is_lock_free _USING_IF_EXISTS;
    using std::atomic_llong _USING_IF_EXISTS;
    using std::atomic_load _USING_IF_EXISTS;
    using std::atomic_load_explicit _USING_IF_EXISTS;
    using std::atomic_long _USING_IF_EXISTS;
    using std::atomic_notify_all _USING_IF_EXISTS;
    using std::atomic_notify_one _USING_IF_EXISTS;
    using std::atomic_ptrdiff_t _USING_IF_EXISTS;
    using std::atomic_schar _USING_IF_EXISTS;
    using std::atomic_short _USING_IF_EXISTS;
    using std::atomic_signal_fence _USING_IF_EXISTS;
    using std::atomic_signed_lock_free _USING_IF_EXISTS;
    using std::atomic_size_t _USING_IF_EXISTS;
    using std::atomic_store _USING_IF_EXISTS;
    using std::atomic_store_explicit _USING_IF_EXISTS;
    using std::atomic_thread_fence _USING_IF_EXISTS;
    using std::atomic_uchar _USING_IF_EXISTS;
    using std::atomic_uint _USING_IF_EXISTS;
    using std::atomic_uint16_t _USING_IF_EXISTS;
    using std::atomic_uint32_t _USING_IF_EXISTS;
    using std::atomic_uint64_t _USING_IF_EXISTS;
    using std::atomic_uint8_t _USING_IF_EXISTS;
    using std::atomic_uint_fast16_t _USING_IF_EXISTS;
    using std::atomic_uint_fast32_t _USING_IF_EXISTS;
    using std::atomic_uint_fast64_t _USING_IF_EXISTS;
    using std::atomic_uint_fast8_t _USING_IF_EXISTS;
    using std::atomic_uint_least16_t _USING_IF_EXISTS;
    using std::atomic_uint_least32_t _USING_IF_EXISTS;
    using std::atomic_uint_least64_t _USING_IF_EXISTS;
    using std::atomic_uint_least8_t _USING_IF_EXISTS;
    using std::atomic_uintmax_t _USING_IF_EXISTS;
    using std::atomic_uintptr_t _USING_IF_EXISTS;
    using std::atomic_ullong _USING_IF_EXISTS;
    using std::atomic_ulong _USING_IF_EXISTS;
    using std::atomic_unsigned_lock_free _USING_IF_EXISTS;
    using std::atomic_ushort _USING_IF_EXISTS;
    using std::atomic_wait _USING_IF_EXISTS;
    using std::atomic_wait_explicit _USING_IF_EXISTS;
    using std::atomic_wchar_t _USING_IF_EXISTS;
    using std::kill_dependency _USING_IF_EXISTS;
    using std::memory_order _USING_IF_EXISTS;
    using std::memory_order_acq_rel _USING_IF_EXISTS;
    using std::memory_order_acquire _USING_IF_EXISTS;
    using std::memory_order_consume _USING_IF_EXISTS;
    using std::memory_order_relaxed _USING_IF_EXISTS;
    using std::memory_order_release _USING_IF_EXISTS;
    using std::memory_order_seq_cst _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::barrier _USING_IF_EXISTS;
}

export namespace std
{
    using std::bit_cast _USING_IF_EXISTS;
    using std::bit_ceil _USING_IF_EXISTS;
    using std::bit_floor _USING_IF_EXISTS;
    using std::bit_width _USING_IF_EXISTS;
    using std::countl_one _USING_IF_EXISTS;
    using std::countl_zero _USING_IF_EXISTS;
    using std::countr_one _USING_IF_EXISTS;
    using std::countr_zero _USING_IF_EXISTS;
    using std::endian _USING_IF_EXISTS;
    using std::has_single_bit _USING_IF_EXISTS;
    using std::popcount _USING_IF_EXISTS;
    using std::rotl _USING_IF_EXISTS;
    using std::rotr _USING_IF_EXISTS;
    using std::byteswap _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::bitset _USING_IF_EXISTS;
    // using std::operator& _USING_IF_EXISTS;
    using std::operator| _USING_IF_EXISTS;
    using std::operator^ _USING_IF_EXISTS;
    using std::operator>> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::isalnum _USING_IF_EXISTS;
    using std::isalpha _USING_IF_EXISTS;
    using std::isblank _USING_IF_EXISTS;
    using std::iscntrl _USING_IF_EXISTS;
    using std::isdigit _USING_IF_EXISTS;
    using std::isgraph _USING_IF_EXISTS;
    using std::islower _USING_IF_EXISTS;
    using std::isprint _USING_IF_EXISTS;
    using std::ispunct _USING_IF_EXISTS;
    using std::isspace _USING_IF_EXISTS;
    using std::isupper _USING_IF_EXISTS;
    using std::isxdigit _USING_IF_EXISTS;
    using std::tolower _USING_IF_EXISTS;
    using std::toupper _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::feclearexcept _USING_IF_EXISTS;
    using std::fegetenv _USING_IF_EXISTS;
    using std::fegetexceptflag _USING_IF_EXISTS;
    using std::fegetround _USING_IF_EXISTS;
    using std::feholdexcept _USING_IF_EXISTS;
    using std::fenv_t _USING_IF_EXISTS;
    using std::feraiseexcept _USING_IF_EXISTS;
    using std::fesetenv _USING_IF_EXISTS;
    using std::fesetexceptflag _USING_IF_EXISTS;
    using std::fesetround _USING_IF_EXISTS;
    using std::fetestexcept _USING_IF_EXISTS;
    using std::feupdateenv _USING_IF_EXISTS;
    using std::fexcept_t _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::chars_format _USING_IF_EXISTS;
    // using std::operator& _USING_IF_EXISTS;
    using std::operator&= _USING_IF_EXISTS;
    // using std::operator^ _USING_IF_EXISTS;
    using std::operator^= _USING_IF_EXISTS;
    // using std::operator| _USING_IF_EXISTS;
    using std::operator|= _USING_IF_EXISTS;
    using std::operator~ _USING_IF_EXISTS;
    using std::from_chars _USING_IF_EXISTS;
    using std::from_chars_result _USING_IF_EXISTS;
    using std::to_chars _USING_IF_EXISTS;
    using std::to_chars_result _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    namespace chrono
    {
        using std::chrono::duration _USING_IF_EXISTS;
        using std::chrono::time_point _USING_IF_EXISTS;
    } // namespace chrono

    using std::common_type _USING_IF_EXISTS;
    namespace chrono
    {
        // inline namespace _V2 // bug: ICE
        // {
        //     using std::chrono::_V2::steady_clock _USING_IF_EXISTS;
        //     using std::chrono::_V2::system_clock _USING_IF_EXISTS;
        //     using std::chrono::_V2::high_resolution_clock _USING_IF_EXISTS;
        // }
        using std::chrono::duration_values _USING_IF_EXISTS;
        using std::chrono::treat_as_floating_point _USING_IF_EXISTS;
        using std::chrono::treat_as_floating_point_v _USING_IF_EXISTS;
        using std::chrono::operator+ _USING_IF_EXISTS;
        using std::chrono::operator- _USING_IF_EXISTS;
        using std::chrono::operator* _USING_IF_EXISTS;
        using std::chrono::operator/ _USING_IF_EXISTS;
        using std::chrono::operator% _USING_IF_EXISTS;
        using std::chrono::operator== _USING_IF_EXISTS;
        // using std::chrono::operator!= _USING_IF_EXISTS; // removed in C++20
        using std::chrono::operator< _USING_IF_EXISTS;
        using std::chrono::operator> _USING_IF_EXISTS;
        using std::chrono::operator<= _USING_IF_EXISTS;
        using std::chrono::operator>= _USING_IF_EXISTS;
        // using std::chrono::operator<=> _USING_IF_EXISTS;
        using std::chrono::ceil _USING_IF_EXISTS;
        using std::chrono::duration_cast _USING_IF_EXISTS;
        using std::chrono::floor _USING_IF_EXISTS;
        using std::chrono::round _USING_IF_EXISTS;
        using std::chrono::operator<< _USING_IF_EXISTS;
        using std::chrono::abs _USING_IF_EXISTS;
        using std::chrono::day _USING_IF_EXISTS;
        using std::chrono::days _USING_IF_EXISTS;
        using std::chrono::file_clock _USING_IF_EXISTS;
        using std::chrono::file_time _USING_IF_EXISTS;
        using std::chrono::hh_mm_ss _USING_IF_EXISTS;
        using std::chrono::high_resolution_clock _USING_IF_EXISTS;
        using std::chrono::hours _USING_IF_EXISTS;
        using std::chrono::is_am _USING_IF_EXISTS;
        using std::chrono::is_pm _USING_IF_EXISTS;
        using std::chrono::last_spec _USING_IF_EXISTS;
        using std::chrono::local_days _USING_IF_EXISTS;
        using std::chrono::local_seconds _USING_IF_EXISTS;
        using std::chrono::local_t _USING_IF_EXISTS;
        using std::chrono::local_time _USING_IF_EXISTS;
        using std::chrono::make12 _USING_IF_EXISTS;
        using std::chrono::make24 _USING_IF_EXISTS;
        using std::chrono::microseconds _USING_IF_EXISTS;
        using std::chrono::milliseconds _USING_IF_EXISTS;
        using std::chrono::minutes _USING_IF_EXISTS;
        using std::chrono::month _USING_IF_EXISTS;
        using std::chrono::month_day _USING_IF_EXISTS;
        using std::chrono::month_day_last _USING_IF_EXISTS;
        using std::chrono::month_weekday _USING_IF_EXISTS;
        using std::chrono::month_weekday_last _USING_IF_EXISTS;
        using std::chrono::months _USING_IF_EXISTS;
        using std::chrono::nanoseconds _USING_IF_EXISTS;
        using std::chrono::seconds _USING_IF_EXISTS;
        using std::chrono::steady_clock _USING_IF_EXISTS;
        using std::chrono::sys_days _USING_IF_EXISTS;
        using std::chrono::sys_seconds _USING_IF_EXISTS;
        using std::chrono::sys_time _USING_IF_EXISTS;
        using std::chrono::system_clock _USING_IF_EXISTS;
        using std::chrono::time_point_cast _USING_IF_EXISTS;
        using std::chrono::weekday _USING_IF_EXISTS;
        using std::chrono::weekday_indexed _USING_IF_EXISTS;
        using std::chrono::weekday_last _USING_IF_EXISTS;
        using std::chrono::weeks _USING_IF_EXISTS;
        using std::chrono::year _USING_IF_EXISTS;
        using std::chrono::year_month _USING_IF_EXISTS;
        using std::chrono::year_month_day _USING_IF_EXISTS;
        using std::chrono::year_month_day_last _USING_IF_EXISTS;
        using std::chrono::year_month_weekday _USING_IF_EXISTS;
        using std::chrono::year_month_weekday_last _USING_IF_EXISTS;
        using std::chrono::years _USING_IF_EXISTS;
    } // namespace chrono

    using std::formatter _USING_IF_EXISTS;
    namespace chrono
    {
        using std::chrono::April _USING_IF_EXISTS;
        using std::chrono::August _USING_IF_EXISTS;
        using std::chrono::December _USING_IF_EXISTS;
        using std::chrono::February _USING_IF_EXISTS;
        using std::chrono::Friday _USING_IF_EXISTS;
        using std::chrono::January _USING_IF_EXISTS;
        using std::chrono::July _USING_IF_EXISTS;
        using std::chrono::June _USING_IF_EXISTS;
        using std::chrono::last _USING_IF_EXISTS;
        using std::chrono::March _USING_IF_EXISTS;
        using std::chrono::May _USING_IF_EXISTS;
        using std::chrono::Monday _USING_IF_EXISTS;
        using std::chrono::November _USING_IF_EXISTS;
        using std::chrono::October _USING_IF_EXISTS;
        using std::chrono::Saturday _USING_IF_EXISTS;
        using std::chrono::September _USING_IF_EXISTS;
        using std::chrono::Sunday _USING_IF_EXISTS;
        using std::chrono::Thursday _USING_IF_EXISTS;
        using std::chrono::Tuesday _USING_IF_EXISTS;
        using std::chrono::Wednesday _USING_IF_EXISTS;
    } // namespace chrono
} // namespace std

export namespace std
{
    inline namespace literals
    {
        inline namespace chrono_literals
        {
            using std::literals::chrono_literals::operator""h _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""min _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""s _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""ms _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""us _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""ns _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""d _USING_IF_EXISTS;
            using std::literals::chrono_literals::operator""y _USING_IF_EXISTS;
        } // inline namespace chrono_literals
    } // inline namespace literals
} // namespace std
export namespace std
{
    using std::imaxabs _USING_IF_EXISTS;
    using std::imaxdiv _USING_IF_EXISTS;
    using std::imaxdiv_t _USING_IF_EXISTS;
    using std::strtoimax _USING_IF_EXISTS;
    using std::strtoumax _USING_IF_EXISTS;
    using std::wcstoimax _USING_IF_EXISTS;
    using std::wcstoumax _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::lconv _USING_IF_EXISTS;
    using std::localeconv _USING_IF_EXISTS;
    using std::setlocale _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::abs _USING_IF_EXISTS;
    using std::acos _USING_IF_EXISTS;
    using std::acosf _USING_IF_EXISTS;
    using std::acosh _USING_IF_EXISTS;
    using std::acoshf _USING_IF_EXISTS;
    using std::acoshl _USING_IF_EXISTS;
    using std::acosl _USING_IF_EXISTS;
    using std::asin _USING_IF_EXISTS;
    using std::asinf _USING_IF_EXISTS;
    using std::asinh _USING_IF_EXISTS;
    using std::asinhf _USING_IF_EXISTS;
    using std::asinhl _USING_IF_EXISTS;
    using std::asinl _USING_IF_EXISTS;
    using std::atan _USING_IF_EXISTS;
    using std::atan2 _USING_IF_EXISTS;
    using std::atan2f _USING_IF_EXISTS;
    using std::atan2l _USING_IF_EXISTS;
    using std::atanf _USING_IF_EXISTS;
    using std::atanh _USING_IF_EXISTS;
    using std::atanhf _USING_IF_EXISTS;
    using std::atanhl _USING_IF_EXISTS;
    using std::atanl _USING_IF_EXISTS;
    using std::cbrt _USING_IF_EXISTS;
    using std::cbrtf _USING_IF_EXISTS;
    using std::cbrtl _USING_IF_EXISTS;
    using std::ceil _USING_IF_EXISTS;
    using std::ceilf _USING_IF_EXISTS;
    using std::ceill _USING_IF_EXISTS;
    using std::copysign _USING_IF_EXISTS;
    using std::copysignf _USING_IF_EXISTS;
    using std::copysignl _USING_IF_EXISTS;
    using std::cos _USING_IF_EXISTS;
    using std::cosf _USING_IF_EXISTS;
    using std::cosh _USING_IF_EXISTS;
    using std::coshf _USING_IF_EXISTS;
    using std::coshl _USING_IF_EXISTS;
    using std::cosl _USING_IF_EXISTS;
    using std::double_t _USING_IF_EXISTS;
    using std::erf _USING_IF_EXISTS;
    using std::erfc _USING_IF_EXISTS;
    using std::erfcf _USING_IF_EXISTS;
    using std::erfcl _USING_IF_EXISTS;
    using std::erff _USING_IF_EXISTS;
    using std::erfl _USING_IF_EXISTS;
    using std::exp _USING_IF_EXISTS;
    using std::exp2 _USING_IF_EXISTS;
    using std::exp2f _USING_IF_EXISTS;
    using std::exp2l _USING_IF_EXISTS;
    using std::expf _USING_IF_EXISTS;
    using std::expl _USING_IF_EXISTS;
    using std::expm1 _USING_IF_EXISTS;
    using std::expm1f _USING_IF_EXISTS;
    using std::expm1l _USING_IF_EXISTS;
    using std::fabs _USING_IF_EXISTS;
    using std::fabsf _USING_IF_EXISTS;
    using std::fabsl _USING_IF_EXISTS;
    using std::fdim _USING_IF_EXISTS;
    using std::fdimf _USING_IF_EXISTS;
    using std::fdiml _USING_IF_EXISTS;
    using std::float_t _USING_IF_EXISTS;
    using std::floor _USING_IF_EXISTS;
    using std::floorf _USING_IF_EXISTS;
    using std::floorl _USING_IF_EXISTS;
    using std::fma _USING_IF_EXISTS;
    using std::fmaf _USING_IF_EXISTS;
    using std::fmal _USING_IF_EXISTS;
    using std::fmax _USING_IF_EXISTS;
    using std::fmaxf _USING_IF_EXISTS;
    using std::fmaxl _USING_IF_EXISTS;
    using std::fmin _USING_IF_EXISTS;
    using std::fminf _USING_IF_EXISTS;
    using std::fminl _USING_IF_EXISTS;
    using std::fmod _USING_IF_EXISTS;
    using std::fmodf _USING_IF_EXISTS;
    using std::fmodl _USING_IF_EXISTS;
    using std::fpclassify _USING_IF_EXISTS;
    using std::frexp _USING_IF_EXISTS;
    using std::frexpf _USING_IF_EXISTS;
    using std::frexpl _USING_IF_EXISTS;
    using std::hypot _USING_IF_EXISTS;
    using std::hypotf _USING_IF_EXISTS;
    using std::hypotl _USING_IF_EXISTS;
    using std::ilogb _USING_IF_EXISTS;
    using std::ilogbf _USING_IF_EXISTS;
    using std::ilogbl _USING_IF_EXISTS;
    using std::isfinite _USING_IF_EXISTS;
    using std::isgreater _USING_IF_EXISTS;
    using std::isgreaterequal _USING_IF_EXISTS;
    using std::isinf _USING_IF_EXISTS;
    using std::isless _USING_IF_EXISTS;
    using std::islessequal _USING_IF_EXISTS;
    using std::islessgreater _USING_IF_EXISTS;
    using std::isnan _USING_IF_EXISTS;
    using std::isnormal _USING_IF_EXISTS;
    using std::isunordered _USING_IF_EXISTS;
    using std::ldexp _USING_IF_EXISTS;
    using std::ldexpf _USING_IF_EXISTS;
    using std::ldexpl _USING_IF_EXISTS;
    using std::lerp _USING_IF_EXISTS;
    using std::lgamma _USING_IF_EXISTS;
    using std::lgammaf _USING_IF_EXISTS;
    using std::lgammal _USING_IF_EXISTS;
    using std::llrint _USING_IF_EXISTS;
    using std::llrintf _USING_IF_EXISTS;
    using std::llrintl _USING_IF_EXISTS;
    using std::llround _USING_IF_EXISTS;
    using std::llroundf _USING_IF_EXISTS;
    using std::llroundl _USING_IF_EXISTS;
    using std::log _USING_IF_EXISTS;
    using std::log10 _USING_IF_EXISTS;
    using std::log10f _USING_IF_EXISTS;
    using std::log10l _USING_IF_EXISTS;
    using std::log1p _USING_IF_EXISTS;
    using std::log1pf _USING_IF_EXISTS;
    using std::log1pl _USING_IF_EXISTS;
    using std::log2 _USING_IF_EXISTS;
    using std::log2f _USING_IF_EXISTS;
    using std::log2l _USING_IF_EXISTS;
    using std::logb _USING_IF_EXISTS;
    using std::logbf _USING_IF_EXISTS;
    using std::logbl _USING_IF_EXISTS;
    using std::logf _USING_IF_EXISTS;
    using std::logl _USING_IF_EXISTS;
    using std::lrint _USING_IF_EXISTS;
    using std::lrintf _USING_IF_EXISTS;
    using std::lrintl _USING_IF_EXISTS;
    using std::lround _USING_IF_EXISTS;
    using std::lroundf _USING_IF_EXISTS;
    using std::lroundl _USING_IF_EXISTS;
    using std::modf _USING_IF_EXISTS;
    using std::modff _USING_IF_EXISTS;
    using std::modfl _USING_IF_EXISTS;
    using std::nan _USING_IF_EXISTS;
    using std::nanf _USING_IF_EXISTS;
    using std::nanl _USING_IF_EXISTS;
    using std::nearbyint _USING_IF_EXISTS;
    using std::nearbyintf _USING_IF_EXISTS;
    using std::nearbyintl _USING_IF_EXISTS;
    using std::nextafter _USING_IF_EXISTS;
    using std::nextafterf _USING_IF_EXISTS;
    using std::nextafterl _USING_IF_EXISTS;
    using std::nexttoward _USING_IF_EXISTS;
    using std::nexttowardf _USING_IF_EXISTS;
    using std::nexttowardl _USING_IF_EXISTS;
    using std::pow _USING_IF_EXISTS;
    using std::powf _USING_IF_EXISTS;
    using std::powl _USING_IF_EXISTS;
    using std::remainder _USING_IF_EXISTS;
    using std::remainderf _USING_IF_EXISTS;
    using std::remainderl _USING_IF_EXISTS;
    using std::remquo _USING_IF_EXISTS;
    using std::remquof _USING_IF_EXISTS;
    using std::remquol _USING_IF_EXISTS;
    using std::rint _USING_IF_EXISTS;
    using std::rintf _USING_IF_EXISTS;
    using std::rintl _USING_IF_EXISTS;
    using std::round _USING_IF_EXISTS;
    using std::roundf _USING_IF_EXISTS;
    using std::roundl _USING_IF_EXISTS;
    using std::scalbln _USING_IF_EXISTS;
    using std::scalblnf _USING_IF_EXISTS;
    using std::scalblnl _USING_IF_EXISTS;
    using std::scalbn _USING_IF_EXISTS;
    using std::scalbnf _USING_IF_EXISTS;
    using std::scalbnl _USING_IF_EXISTS;
    using std::signbit _USING_IF_EXISTS;
    using std::sin _USING_IF_EXISTS;
    using std::sinf _USING_IF_EXISTS;
    using std::sinh _USING_IF_EXISTS;
    using std::sinhf _USING_IF_EXISTS;
    using std::sinhl _USING_IF_EXISTS;
    using std::sinl _USING_IF_EXISTS;
    using std::sqrt _USING_IF_EXISTS;
    using std::sqrtf _USING_IF_EXISTS;
    using std::sqrtl _USING_IF_EXISTS;
    using std::tan _USING_IF_EXISTS;
    using std::tanf _USING_IF_EXISTS;
    using std::tanh _USING_IF_EXISTS;
    using std::tanhf _USING_IF_EXISTS;
    using std::tanhl _USING_IF_EXISTS;
    using std::tanl _USING_IF_EXISTS;
    using std::tgamma _USING_IF_EXISTS;
    using std::tgammaf _USING_IF_EXISTS;
    using std::tgammal _USING_IF_EXISTS;
    using std::trunc _USING_IF_EXISTS;
    using std::truncf _USING_IF_EXISTS;
    using std::truncl _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::codecvt_mode _USING_IF_EXISTS;
    using std::codecvt_utf16 _USING_IF_EXISTS;
    using std::codecvt_utf8 _USING_IF_EXISTS;
    using std::codecvt_utf8_utf16 _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::common_comparison_category _USING_IF_EXISTS;
    using std::common_comparison_category_t _USING_IF_EXISTS;
    using std::compare_three_way _USING_IF_EXISTS;
    using std::compare_three_way_result _USING_IF_EXISTS;
    using std::compare_three_way_result_t _USING_IF_EXISTS;
    using std::is_eq _USING_IF_EXISTS;
    using std::is_gt _USING_IF_EXISTS;
    using std::is_gteq _USING_IF_EXISTS;
    using std::is_lt _USING_IF_EXISTS;
    using std::is_lteq _USING_IF_EXISTS;
    using std::is_neq _USING_IF_EXISTS;
    using std::partial_ordering _USING_IF_EXISTS;
    using std::strong_ordering _USING_IF_EXISTS;
    using std::three_way_comparable _USING_IF_EXISTS;
    using std::three_way_comparable_with _USING_IF_EXISTS;
    using std::weak_ordering _USING_IF_EXISTS;
    inline namespace _Cpo
    {
        using std::_Cpo::compare_partial_order_fallback _USING_IF_EXISTS;
        using std::_Cpo::compare_strong_order_fallback _USING_IF_EXISTS;
        using std::_Cpo::compare_weak_order_fallback _USING_IF_EXISTS;
        using std::_Cpo::partial_order _USING_IF_EXISTS;
        using std::_Cpo::strong_order _USING_IF_EXISTS;
        using std::_Cpo::weak_order _USING_IF_EXISTS;
    } // namespace _Cpo
} // namespace std

export namespace std
{
    using std::complex _USING_IF_EXISTS;
    using std::operator+ _USING_IF_EXISTS;
    using std::operator- _USING_IF_EXISTS;
    using std::operator* _USING_IF_EXISTS;
    using std::operator/ _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator>> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    // using std::abs _USING_IF_EXISTS;
    // using std::acos _USING_IF_EXISTS;
    // using std::acosh _USING_IF_EXISTS;
    using std::arg _USING_IF_EXISTS;
    // using std::asin _USING_IF_EXISTS;
    // using std::asinh _USING_IF_EXISTS;
    // using std::atan _USING_IF_EXISTS;
    // using std::atanh _USING_IF_EXISTS;
    using std::conj _USING_IF_EXISTS;
    // using std::cos _USING_IF_EXISTS;
    // using std::cosh _USING_IF_EXISTS;
    // using std::exp _USING_IF_EXISTS;
    using std::imag _USING_IF_EXISTS;
    // using std::log _USING_IF_EXISTS;
    // using std::log10 _USING_IF_EXISTS;
    using std::norm _USING_IF_EXISTS;
    using std::polar _USING_IF_EXISTS;
    // using std::pow _USING_IF_EXISTS;
    using std::proj _USING_IF_EXISTS;
    using std::real _USING_IF_EXISTS;
    // using std::sin _USING_IF_EXISTS;
    // using std::sinh _USING_IF_EXISTS;
    // using std::sqrt _USING_IF_EXISTS;
    // using std::tan _USING_IF_EXISTS;
    // using std::tanh _USING_IF_EXISTS;
    inline namespace literals
    {
        inline namespace complex_literals
        {
            using std::operator""il _USING_IF_EXISTS;
            using std::operator""i _USING_IF_EXISTS;
            using std::operator""if _USING_IF_EXISTS;
        } // namespace complex_literals
    } // namespace literals
} // namespace std

export namespace std
{
    using std::assignable_from _USING_IF_EXISTS;
    using std::common_reference_with _USING_IF_EXISTS;
    using std::common_with _USING_IF_EXISTS;
    using std::convertible_to _USING_IF_EXISTS;
    using std::derived_from _USING_IF_EXISTS;
    using std::floating_point _USING_IF_EXISTS;
    using std::integral _USING_IF_EXISTS;
    using std::same_as _USING_IF_EXISTS;
    using std::signed_integral _USING_IF_EXISTS;
    using std::unsigned_integral _USING_IF_EXISTS;
    namespace ranges
    {
        inline namespace _Cpo
        {
            using std::ranges::_Cpo::swap _USING_IF_EXISTS;
        }
    } // namespace ranges
    using std::constructible_from _USING_IF_EXISTS;
    using std::copy_constructible _USING_IF_EXISTS;
    using std::copyable _USING_IF_EXISTS;
    using std::default_initializable _USING_IF_EXISTS;
    using std::destructible _USING_IF_EXISTS;
    using std::equality_comparable _USING_IF_EXISTS;
    using std::equality_comparable_with _USING_IF_EXISTS;
    using std::equivalence_relation _USING_IF_EXISTS;
    using std::invocable _USING_IF_EXISTS;
    using std::movable _USING_IF_EXISTS;
    using std::move_constructible _USING_IF_EXISTS;
    using std::predicate _USING_IF_EXISTS;
    using std::regular _USING_IF_EXISTS;
    using std::regular_invocable _USING_IF_EXISTS;
    using std::relation _USING_IF_EXISTS;
    using std::semiregular _USING_IF_EXISTS;
    using std::strict_weak_order _USING_IF_EXISTS;
    using std::swappable _USING_IF_EXISTS;
    using std::swappable_with _USING_IF_EXISTS;
    using std::totally_ordered _USING_IF_EXISTS;
    using std::totally_ordered_with _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::condition_variable _USING_IF_EXISTS;
    using std::condition_variable_any _USING_IF_EXISTS;
    using std::cv_status _USING_IF_EXISTS;
    using std::notify_all_at_thread_exit _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::coroutine_handle _USING_IF_EXISTS;
    using std::coroutine_traits _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    using std::noop_coroutine _USING_IF_EXISTS;
    using std::noop_coroutine_handle _USING_IF_EXISTS;
    using std::noop_coroutine_promise _USING_IF_EXISTS;
    using std::suspend_always _USING_IF_EXISTS;
    using std::suspend_never _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::jmp_buf _USING_IF_EXISTS;
    using std::longjmp _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::raise _USING_IF_EXISTS;
    using std::sig_atomic_t _USING_IF_EXISTS;
    using std::signal _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::va_list _USING_IF_EXISTS;
}
export namespace std
{
    using std::byte _USING_IF_EXISTS;
    using std::max_align_t _USING_IF_EXISTS;
    using std::nullptr_t _USING_IF_EXISTS;
    using std::ptrdiff_t _USING_IF_EXISTS;
    using std::size_t _USING_IF_EXISTS;
    using std::ssize_t _USING_IF_EXISTS;
    using std::operator<<= _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::operator>>= _USING_IF_EXISTS;
    using std::operator>> _USING_IF_EXISTS;
    // using std::operator|= _USING_IF_EXISTS;
    // using std::operator| _USING_IF_EXISTS;
    // using std::operator&= _USING_IF_EXISTS;
    // using std::operator& _USING_IF_EXISTS;
    // using std::operator^= _USING_IF_EXISTS;
    // using std::operator^ _USING_IF_EXISTS;
    // using std::operator~ _USING_IF_EXISTS;
    using std::to_integer _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::int8_t _USING_IF_EXISTS;
    using std::int16_t _USING_IF_EXISTS;
    using std::int32_t _USING_IF_EXISTS;
    using std::int64_t _USING_IF_EXISTS;
    using std::int_fast16_t _USING_IF_EXISTS;
    using std::int_fast32_t _USING_IF_EXISTS;
    using std::int_fast64_t _USING_IF_EXISTS;
    using std::int_fast8_t _USING_IF_EXISTS;
    using std::int_least16_t _USING_IF_EXISTS;
    using std::int_least32_t _USING_IF_EXISTS;
    using std::int_least64_t _USING_IF_EXISTS;
    using std::int_least8_t _USING_IF_EXISTS;
    using std::intmax_t _USING_IF_EXISTS;
    using std::intptr_t _USING_IF_EXISTS;
    using std::uint8_t _USING_IF_EXISTS;
    using std::uint16_t _USING_IF_EXISTS;
    using std::uint32_t _USING_IF_EXISTS;
    using std::uint64_t _USING_IF_EXISTS;
    using std::uint_fast16_t _USING_IF_EXISTS;
    using std::uint_fast32_t _USING_IF_EXISTS;
    using std::uint_fast64_t _USING_IF_EXISTS;
    using std::uint_fast8_t _USING_IF_EXISTS;
    using std::uint_least16_t _USING_IF_EXISTS;
    using std::uint_least32_t _USING_IF_EXISTS;
    using std::uint_least64_t _USING_IF_EXISTS;
    using std::uint_least8_t _USING_IF_EXISTS;
    using std::uintmax_t _USING_IF_EXISTS;
    using std::uintptr_t _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::clearerr _USING_IF_EXISTS;
    using std::fclose _USING_IF_EXISTS;
    using std::feof _USING_IF_EXISTS;
    using std::ferror _USING_IF_EXISTS;
    using std::fflush _USING_IF_EXISTS;
    using std::fgetc _USING_IF_EXISTS;
    using std::fgetpos _USING_IF_EXISTS;
    using std::fgets _USING_IF_EXISTS;
    using std::FILE _USING_IF_EXISTS;
    using std::fopen _USING_IF_EXISTS;
    using std::fpos_t _USING_IF_EXISTS;
    using std::fprintf _USING_IF_EXISTS;
    using std::fputc _USING_IF_EXISTS;
    using std::fputs _USING_IF_EXISTS;
    using std::fread _USING_IF_EXISTS;
    using std::freopen _USING_IF_EXISTS;
    using std::fscanf _USING_IF_EXISTS;
    using std::fseek _USING_IF_EXISTS;
    using std::fsetpos _USING_IF_EXISTS;
    using std::ftell _USING_IF_EXISTS;
    using std::fwrite _USING_IF_EXISTS;
    using std::getc _USING_IF_EXISTS;
    using std::getchar _USING_IF_EXISTS;
    using std::perror _USING_IF_EXISTS;
    using std::printf _USING_IF_EXISTS;
    using std::putc _USING_IF_EXISTS;
    using std::putchar _USING_IF_EXISTS;
    using std::puts _USING_IF_EXISTS;
    using std::remove _USING_IF_EXISTS;
    using std::rename _USING_IF_EXISTS;
    using std::rewind _USING_IF_EXISTS;
    using std::scanf _USING_IF_EXISTS;
    using std::setbuf _USING_IF_EXISTS;
    using std::setvbuf _USING_IF_EXISTS;
    using std::size_t _USING_IF_EXISTS;
    using std::snprintf _USING_IF_EXISTS;
    using std::sprintf _USING_IF_EXISTS;
    using std::sscanf _USING_IF_EXISTS;
    using std::tmpfile _USING_IF_EXISTS;
    using std::tmpnam _USING_IF_EXISTS;
    using std::ungetc _USING_IF_EXISTS;
    using std::vfprintf _USING_IF_EXISTS;
    using std::vfscanf _USING_IF_EXISTS;
    using std::vprintf _USING_IF_EXISTS;
    using std::vscanf _USING_IF_EXISTS;
    using std::vsnprintf _USING_IF_EXISTS;
    using std::vsprintf _USING_IF_EXISTS;
    using std::vsscanf _USING_IF_EXISTS;
    using std::asprintf _USING_IF_EXISTS;
    using std::vasprintf _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::_Exit _USING_IF_EXISTS;
    using std::abort _USING_IF_EXISTS;
    // using std::abs _USING_IF_EXISTS;
    using std::aligned_alloc _USING_IF_EXISTS;
    using std::at_quick_exit _USING_IF_EXISTS;
    using std::atexit _USING_IF_EXISTS;
    using std::atof _USING_IF_EXISTS;
    using std::atoi _USING_IF_EXISTS;
    using std::atol _USING_IF_EXISTS;
    using std::atoll _USING_IF_EXISTS;
    using std::bsearch _USING_IF_EXISTS;
    using std::calloc _USING_IF_EXISTS;
    using std::div _USING_IF_EXISTS;
    using std::div_t _USING_IF_EXISTS;
    using std::exit _USING_IF_EXISTS;
    using std::free _USING_IF_EXISTS;
    using std::getenv _USING_IF_EXISTS;
    using std::labs _USING_IF_EXISTS;
    using std::ldiv _USING_IF_EXISTS;
    using std::ldiv_t _USING_IF_EXISTS;
    using std::llabs _USING_IF_EXISTS;
    using std::lldiv _USING_IF_EXISTS;
    using std::lldiv_t _USING_IF_EXISTS; // bug: needs __gnu_cxx namespace declaration in module purview
    using std::malloc _USING_IF_EXISTS;
    using std::mblen _USING_IF_EXISTS;
    using std::mbstowcs _USING_IF_EXISTS;
    using std::mbtowc _USING_IF_EXISTS;
    using std::qsort _USING_IF_EXISTS;
    using std::quick_exit _USING_IF_EXISTS;
    using std::rand _USING_IF_EXISTS;
    using std::realloc _USING_IF_EXISTS;
    using std::size_t _USING_IF_EXISTS;
    using std::srand _USING_IF_EXISTS;
    using std::strtod _USING_IF_EXISTS;
    using std::strtof _USING_IF_EXISTS;
    using std::strtol _USING_IF_EXISTS;
    using std::strtold _USING_IF_EXISTS;
    using std::strtoll _USING_IF_EXISTS;
    using std::strtoul _USING_IF_EXISTS;
    using std::strtoull _USING_IF_EXISTS;
    using std::system _USING_IF_EXISTS;
    using std::wcstombs _USING_IF_EXISTS;
    using std::wctomb _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using ::memchr _USING_IF_EXISTS;
    using std::memcmp _USING_IF_EXISTS;
    using std::memcpy _USING_IF_EXISTS;
    using std::memmove _USING_IF_EXISTS;
    using std::memset _USING_IF_EXISTS;
    using std::size_t _USING_IF_EXISTS;
    using std::strcat _USING_IF_EXISTS;
    using std::strchr _USING_IF_EXISTS;
    using std::strcmp _USING_IF_EXISTS;
    using std::strcoll _USING_IF_EXISTS;
    using std::strcpy _USING_IF_EXISTS;
    using std::strcspn _USING_IF_EXISTS;
    using std::strerror _USING_IF_EXISTS;
    using std::strlen _USING_IF_EXISTS;
    using std::strncat _USING_IF_EXISTS;
    using std::strncmp _USING_IF_EXISTS;
    using std::strncpy _USING_IF_EXISTS;
    using std::strpbrk _USING_IF_EXISTS;
    using std::strrchr _USING_IF_EXISTS;
    using std::strspn _USING_IF_EXISTS;
    using std::strstr _USING_IF_EXISTS;
    using std::strtok _USING_IF_EXISTS;
    using std::strxfrm _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::asctime _USING_IF_EXISTS;
    using std::clock _USING_IF_EXISTS;
    using std::clock_t _USING_IF_EXISTS;
    using std::ctime _USING_IF_EXISTS;
    using std::difftime _USING_IF_EXISTS;
    using std::gmtime _USING_IF_EXISTS;
    using std::localtime _USING_IF_EXISTS;
    using std::mktime _USING_IF_EXISTS;
    using std::size_t _USING_IF_EXISTS;
    using std::strftime _USING_IF_EXISTS;
    using std::time _USING_IF_EXISTS;
    using std::time_t _USING_IF_EXISTS;
    using std::timespec _USING_IF_EXISTS;
    using std::tm _USING_IF_EXISTS;
    using std::timespec_get _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    // using std::mbrtoc8 _USING_IF_EXISTS;
    // using std::c8rtomb _USING_IF_EXISTS;
    using std::mbrtoc16 _USING_IF_EXISTS;
    using std::c16rtomb _USING_IF_EXISTS;
    using std::mbrtoc32 _USING_IF_EXISTS;
    using std::c32rtomb _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::btowc _USING_IF_EXISTS;
    using std::fgetwc _USING_IF_EXISTS;
    using std::fgetws _USING_IF_EXISTS;
    using std::fputwc _USING_IF_EXISTS;
    using std::fputws _USING_IF_EXISTS;
    using std::fwide _USING_IF_EXISTS;
    using std::fwprintf _USING_IF_EXISTS;
    using std::fwscanf _USING_IF_EXISTS;
    using std::getwc _USING_IF_EXISTS;
    using std::getwchar _USING_IF_EXISTS;
    using std::mbrlen _USING_IF_EXISTS;
    using std::mbrtowc _USING_IF_EXISTS;
    using std::mbsinit _USING_IF_EXISTS;
    using std::mbsrtowcs _USING_IF_EXISTS;
    using std::mbstate_t _USING_IF_EXISTS;
    using std::putwc _USING_IF_EXISTS;
    using std::putwchar _USING_IF_EXISTS;
    using std::size_t _USING_IF_EXISTS;
    using std::swprintf _USING_IF_EXISTS;
    using std::swscanf _USING_IF_EXISTS;
    using std::tm _USING_IF_EXISTS;
    using std::ungetwc _USING_IF_EXISTS;
    using std::vfwprintf _USING_IF_EXISTS;
    using std::vfwscanf _USING_IF_EXISTS;
    using std::vswprintf _USING_IF_EXISTS;
    using std::vswscanf _USING_IF_EXISTS;
    using std::vwprintf _USING_IF_EXISTS;
    using std::vwscanf _USING_IF_EXISTS;
    using std::wcrtomb _USING_IF_EXISTS;
    using std::wcscat _USING_IF_EXISTS;
    using std::wcschr _USING_IF_EXISTS;
    using std::wcscmp _USING_IF_EXISTS;
    using std::wcscoll _USING_IF_EXISTS;
    using std::wcscpy _USING_IF_EXISTS;
    using std::wcscspn _USING_IF_EXISTS;
    using std::wcsftime _USING_IF_EXISTS;
    using std::wcslen _USING_IF_EXISTS;
    using std::wcsncat _USING_IF_EXISTS;
    using std::wcsncmp _USING_IF_EXISTS;
    using std::wcsncpy _USING_IF_EXISTS;
    using std::wcspbrk _USING_IF_EXISTS;
    using std::wcsrchr _USING_IF_EXISTS;
    using std::wcsrtombs _USING_IF_EXISTS;
    using std::wcsspn _USING_IF_EXISTS;
    using std::wcsstr _USING_IF_EXISTS;
    using std::wcstod _USING_IF_EXISTS;
    using std::wcstof _USING_IF_EXISTS;
    using std::wcstok _USING_IF_EXISTS;
    using std::wcstol _USING_IF_EXISTS;
    using std::wcstold _USING_IF_EXISTS;
    using std::wcstoll _USING_IF_EXISTS;
    using std::wcstoul _USING_IF_EXISTS;
    using std::wcstoull _USING_IF_EXISTS;
    using std::wcsxfrm _USING_IF_EXISTS;
    using std::wctob _USING_IF_EXISTS;
    using std::wint_t _USING_IF_EXISTS;
    using std::wmemchr _USING_IF_EXISTS;
    using std::wmemcmp _USING_IF_EXISTS;
    using std::wmemcpy _USING_IF_EXISTS;
    using std::wmemmove _USING_IF_EXISTS;
    using std::wmemset _USING_IF_EXISTS;
    using std::wprintf _USING_IF_EXISTS;
    using std::wscanf _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::iswalnum _USING_IF_EXISTS;
    using std::iswalpha _USING_IF_EXISTS;
    using std::iswblank _USING_IF_EXISTS;
    using std::iswcntrl _USING_IF_EXISTS;
    using std::iswctype _USING_IF_EXISTS;
    using std::iswdigit _USING_IF_EXISTS;
    using std::iswgraph _USING_IF_EXISTS;
    using std::iswlower _USING_IF_EXISTS;
    using std::iswprint _USING_IF_EXISTS;
    using std::iswpunct _USING_IF_EXISTS;
    using std::iswspace _USING_IF_EXISTS;
    using std::iswupper _USING_IF_EXISTS;
    using std::iswxdigit _USING_IF_EXISTS;
    using std::towctrans _USING_IF_EXISTS;
    using std::towlower _USING_IF_EXISTS;
    using std::towupper _USING_IF_EXISTS;
    using std::wctrans _USING_IF_EXISTS;
    using std::wctrans_t _USING_IF_EXISTS;
    using std::wctype _USING_IF_EXISTS;
    using std::wctype_t _USING_IF_EXISTS;
    using std::wint_t _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::deque _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::erase _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::deque _USING_IF_EXISTS;
    }
} // namespace std

export namespace std
{
    using std::bad_exception _USING_IF_EXISTS;
    using std::current_exception _USING_IF_EXISTS;
    using std::exception _USING_IF_EXISTS;
    namespace __exception_ptr
    {
        using std::__exception_ptr::exception_ptr _USING_IF_EXISTS;
    }
    using std::exception_ptr _USING_IF_EXISTS; // bug: ICE
    using std::get_terminate _USING_IF_EXISTS;
    using std::make_exception_ptr _USING_IF_EXISTS;
    using std::nested_exception _USING_IF_EXISTS;
    using std::rethrow_exception _USING_IF_EXISTS;
    using std::rethrow_if_nested _USING_IF_EXISTS;
    using std::set_terminate _USING_IF_EXISTS;
    using std::terminate _USING_IF_EXISTS;
    using std::terminate_handler _USING_IF_EXISTS;
    using std::throw_with_nested _USING_IF_EXISTS;
    using std::uncaught_exception _USING_IF_EXISTS;
    using std::uncaught_exceptions _USING_IF_EXISTS;
} // namespace std

export namespace std::filesystem
{
    using std::filesystem::begin _USING_IF_EXISTS;
    using std::filesystem::copy_options _USING_IF_EXISTS;
    using std::filesystem::directory_entry _USING_IF_EXISTS;
    using std::filesystem::directory_iterator _USING_IF_EXISTS;
    using std::filesystem::directory_options _USING_IF_EXISTS;
    using std::filesystem::end _USING_IF_EXISTS;
    using std::filesystem::file_status _USING_IF_EXISTS;
    using std::filesystem::file_time_type _USING_IF_EXISTS;
    using std::filesystem::file_type _USING_IF_EXISTS;
    using std::filesystem::filesystem_error _USING_IF_EXISTS;
    using std::filesystem::hash_value _USING_IF_EXISTS;
    using std::filesystem::path _USING_IF_EXISTS;
    using std::filesystem::perm_options _USING_IF_EXISTS;
    using std::filesystem::perms _USING_IF_EXISTS;
    using std::filesystem::recursive_directory_iterator _USING_IF_EXISTS;
    using std::filesystem::space_info _USING_IF_EXISTS;
    using std::filesystem::swap _USING_IF_EXISTS;
    using std::filesystem::operator& _USING_IF_EXISTS;
    using std::filesystem::operator&= _USING_IF_EXISTS;
    using std::filesystem::operator^ _USING_IF_EXISTS;
    using std::filesystem::operator^= _USING_IF_EXISTS;
    using std::filesystem::operator| _USING_IF_EXISTS;
    using std::filesystem::operator|= _USING_IF_EXISTS;
    using std::filesystem::operator~ _USING_IF_EXISTS;
    using std::filesystem::absolute _USING_IF_EXISTS;
    using std::filesystem::canonical _USING_IF_EXISTS;
    using std::filesystem::copy _USING_IF_EXISTS;
    using std::filesystem::copy_file _USING_IF_EXISTS;
    using std::filesystem::copy_symlink _USING_IF_EXISTS;
    using std::filesystem::create_directories _USING_IF_EXISTS;
    using std::filesystem::create_directory _USING_IF_EXISTS;
    using std::filesystem::create_directory_symlink _USING_IF_EXISTS;
    using std::filesystem::create_hard_link _USING_IF_EXISTS;
    using std::filesystem::create_symlink _USING_IF_EXISTS;
    using std::filesystem::current_path _USING_IF_EXISTS;
    using std::filesystem::equivalent _USING_IF_EXISTS;
    using std::filesystem::exists _USING_IF_EXISTS;
    using std::filesystem::file_size _USING_IF_EXISTS;
    using std::filesystem::hard_link_count _USING_IF_EXISTS;
    using std::filesystem::is_block_file _USING_IF_EXISTS;
    using std::filesystem::is_character_file _USING_IF_EXISTS;
    using std::filesystem::is_directory _USING_IF_EXISTS;
    using std::filesystem::is_empty _USING_IF_EXISTS;
    using std::filesystem::is_fifo _USING_IF_EXISTS;
    using std::filesystem::is_other _USING_IF_EXISTS;
    using std::filesystem::is_regular_file _USING_IF_EXISTS;
    using std::filesystem::is_socket _USING_IF_EXISTS;
    using std::filesystem::is_symlink _USING_IF_EXISTS;
    using std::filesystem::last_write_time _USING_IF_EXISTS;
    using std::filesystem::permissions _USING_IF_EXISTS;
    using std::filesystem::proximate _USING_IF_EXISTS;
    using std::filesystem::read_symlink _USING_IF_EXISTS;
    using std::filesystem::relative _USING_IF_EXISTS;
    using std::filesystem::remove _USING_IF_EXISTS;
    using std::filesystem::remove_all _USING_IF_EXISTS;
    using std::filesystem::rename _USING_IF_EXISTS;
    using std::filesystem::resize_file _USING_IF_EXISTS;
    using std::filesystem::space _USING_IF_EXISTS;
    using std::filesystem::status _USING_IF_EXISTS;
    using std::filesystem::status_known _USING_IF_EXISTS;
    using std::filesystem::symlink_status _USING_IF_EXISTS;
    using std::filesystem::temp_directory_path _USING_IF_EXISTS;
    using std::filesystem::u8path _USING_IF_EXISTS;
    using std::filesystem::weakly_canonical _USING_IF_EXISTS;
} // namespace std::filesystem
export namespace std
{
    using std::hash _USING_IF_EXISTS;
}
export namespace std::ranges
{
    using std::ranges::enable_borrowed_range _USING_IF_EXISTS;
    using std::ranges::enable_view _USING_IF_EXISTS;
} // namespace std::ranges
export namespace std
{
    using std::basic_format_arg _USING_IF_EXISTS;
    using std::basic_format_args _USING_IF_EXISTS;
    using std::basic_format_context _USING_IF_EXISTS;
    using std::basic_format_parse_context _USING_IF_EXISTS;
    using std::basic_format_string _USING_IF_EXISTS;
    using std::format _USING_IF_EXISTS;
    using std::format_args _USING_IF_EXISTS;
    using std::format_context _USING_IF_EXISTS;
    using std::format_error _USING_IF_EXISTS;
    using std::format_parse_context _USING_IF_EXISTS;
    using std::format_string _USING_IF_EXISTS;
    using std::format_to _USING_IF_EXISTS;
    using std::format_to_n _USING_IF_EXISTS;
    using std::format_to_n_result _USING_IF_EXISTS;
    using std::formatted_size _USING_IF_EXISTS;
    using std::formatter _USING_IF_EXISTS;
    using std::make_format_args _USING_IF_EXISTS;
    using std::make_wformat_args _USING_IF_EXISTS;
    using std::vformat _USING_IF_EXISTS;
    using std::vformat_to _USING_IF_EXISTS;
    using std::visit_format_arg _USING_IF_EXISTS;
    using std::wformat_args _USING_IF_EXISTS;
    using std::wformat_context _USING_IF_EXISTS;
    using std::wformat_parse_context _USING_IF_EXISTS;
    using std::wformat_string _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::unexpected _USING_IF_EXISTS;
    using std::bad_expected_access _USING_IF_EXISTS;
    using std::unexpect_t _USING_IF_EXISTS;
    using std::expected _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::forward_list _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::erase _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::forward_list _USING_IF_EXISTS;
    }
} // namespace std

export namespace std
{
    using std::basic_filebuf _USING_IF_EXISTS;
    using std::basic_fstream _USING_IF_EXISTS;
    using std::basic_ifstream _USING_IF_EXISTS;
    using std::basic_ofstream _USING_IF_EXISTS;
    using std::filebuf _USING_IF_EXISTS;
    using std::fstream _USING_IF_EXISTS;
    using std::ifstream _USING_IF_EXISTS;
    using std::ofstream _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::wfilebuf _USING_IF_EXISTS;
    using std::wfstream _USING_IF_EXISTS;
    using std::wifstream _USING_IF_EXISTS;
    using std::wofstream _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::bind _USING_IF_EXISTS;
    using std::bind_front _USING_IF_EXISTS;
    using std::bit_and _USING_IF_EXISTS;
    using std::bit_not _USING_IF_EXISTS;
    using std::bit_or _USING_IF_EXISTS;
    using std::bit_xor _USING_IF_EXISTS;
    using std::compare_three_way _USING_IF_EXISTS;
    using std::cref _USING_IF_EXISTS;
    using std::divides _USING_IF_EXISTS;
    using std::equal_to _USING_IF_EXISTS;
    using std::greater _USING_IF_EXISTS;
    using std::greater_equal _USING_IF_EXISTS;
    using std::identity _USING_IF_EXISTS;
    using std::invoke _USING_IF_EXISTS;
    using std::is_bind_expression _USING_IF_EXISTS;
    using std::is_bind_expression_v _USING_IF_EXISTS;
    using std::is_placeholder _USING_IF_EXISTS;
    using std::is_placeholder_v _USING_IF_EXISTS;
    using std::less _USING_IF_EXISTS;
    using std::less_equal _USING_IF_EXISTS;
    using std::logical_and _USING_IF_EXISTS;
    using std::logical_not _USING_IF_EXISTS;
    using std::logical_or _USING_IF_EXISTS;
    using std::minus _USING_IF_EXISTS;
    using std::modulus _USING_IF_EXISTS;
    using std::multiplies _USING_IF_EXISTS;
    using std::negate _USING_IF_EXISTS;
    using std::not_equal_to _USING_IF_EXISTS;
    using std::not_fn _USING_IF_EXISTS;
    using std::plus _USING_IF_EXISTS;
    using std::ref _USING_IF_EXISTS;
    using std::reference_wrapper _USING_IF_EXISTS;
    namespace placeholders
    {
        using std::placeholders::_1 _USING_IF_EXISTS;
        using std::placeholders::_10 _USING_IF_EXISTS;
        using std::placeholders::_2 _USING_IF_EXISTS;
        using std::placeholders::_3 _USING_IF_EXISTS;
        using std::placeholders::_4 _USING_IF_EXISTS;
        using std::placeholders::_5 _USING_IF_EXISTS;
        using std::placeholders::_6 _USING_IF_EXISTS;
        using std::placeholders::_7 _USING_IF_EXISTS;
        using std::placeholders::_8 _USING_IF_EXISTS;
        using std::placeholders::_9 _USING_IF_EXISTS;
    } // namespace placeholders
    using std::bad_function_call _USING_IF_EXISTS;
    using std::function _USING_IF_EXISTS;
    using std::mem_fn _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::boyer_moore_horspool_searcher _USING_IF_EXISTS;
    using std::boyer_moore_searcher _USING_IF_EXISTS;
    using std::default_searcher _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::equal_to _USING_IF_EXISTS;
        using std::ranges::greater _USING_IF_EXISTS;
        using std::ranges::greater_equal _USING_IF_EXISTS;
        using std::ranges::less _USING_IF_EXISTS;
        using std::ranges::less_equal _USING_IF_EXISTS;
        using std::ranges::not_equal_to _USING_IF_EXISTS;
    } // namespace ranges
} // namespace std

export namespace std
{
    using std::future_errc _USING_IF_EXISTS;
    using std::future_status _USING_IF_EXISTS;
    using std::launch _USING_IF_EXISTS;
    // using std::operator& _USING_IF_EXISTS;
    // using std::operator&= _USING_IF_EXISTS;
    // using std::operator^ _USING_IF_EXISTS;
    // using std::operator^= _USING_IF_EXISTS;
    // using std::operator| _USING_IF_EXISTS;
    // using std::operator|= _USING_IF_EXISTS;
    // using std::operator~ _USING_IF_EXISTS;
    using std::async _USING_IF_EXISTS;
    using std::future _USING_IF_EXISTS;
    using std::future_category _USING_IF_EXISTS;
    using std::future_error _USING_IF_EXISTS;
    using std::is_error_code_enum _USING_IF_EXISTS;
    using std::make_error_code _USING_IF_EXISTS;
    using std::make_error_condition _USING_IF_EXISTS;
    using std::packaged_task _USING_IF_EXISTS;
    using std::promise _USING_IF_EXISTS;
    using std::shared_future _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::uses_allocator _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::begin _USING_IF_EXISTS;
    using std::end _USING_IF_EXISTS;
    using std::initializer_list _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::generator _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::get_money _USING_IF_EXISTS;
    using std::get_time _USING_IF_EXISTS;
    using std::put_money _USING_IF_EXISTS;
    using std::put_time _USING_IF_EXISTS;
    using std::quoted _USING_IF_EXISTS;
    using std::resetiosflags _USING_IF_EXISTS;
    using std::setbase _USING_IF_EXISTS;
    using std::setfill _USING_IF_EXISTS;
    using std::setiosflags _USING_IF_EXISTS;
    using std::setprecision _USING_IF_EXISTS;
    using std::setw _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::fpos _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator- _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::basic_ios _USING_IF_EXISTS;
    using std::boolalpha _USING_IF_EXISTS;
    using std::dec _USING_IF_EXISTS;
    using std::defaultfloat _USING_IF_EXISTS;
    using std::fixed _USING_IF_EXISTS;
    using std::hex _USING_IF_EXISTS;
    using std::hexfloat _USING_IF_EXISTS;
    using std::internal _USING_IF_EXISTS;
    using std::io_errc _USING_IF_EXISTS;
    using std::ios _USING_IF_EXISTS;
    using std::ios_base _USING_IF_EXISTS;
    using std::iostream_category _USING_IF_EXISTS;
    using std::is_error_code_enum _USING_IF_EXISTS;
    using std::left _USING_IF_EXISTS;
    // using std::make_error_code _USING_IF_EXISTS;
    // using std::make_error_condition _USING_IF_EXISTS;
    using std::noboolalpha _USING_IF_EXISTS;
    using std::noshowbase _USING_IF_EXISTS;
    using std::noshowpoint _USING_IF_EXISTS;
    using std::noshowpos _USING_IF_EXISTS;
    using std::noskipws _USING_IF_EXISTS;
    using std::nounitbuf _USING_IF_EXISTS;
    using std::nouppercase _USING_IF_EXISTS;
    using std::oct _USING_IF_EXISTS;
    using std::right _USING_IF_EXISTS;
    using std::scientific _USING_IF_EXISTS;
    using std::showbase _USING_IF_EXISTS;
    using std::showpoint _USING_IF_EXISTS;
    using std::showpos _USING_IF_EXISTS;
    using std::skipws _USING_IF_EXISTS;
    using std::streamoff _USING_IF_EXISTS;
    using std::streamsize _USING_IF_EXISTS;
    using std::unitbuf _USING_IF_EXISTS;
    using std::uppercase _USING_IF_EXISTS;
    using std::wios _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::streampos _USING_IF_EXISTS;
    using std::u16streampos _USING_IF_EXISTS;
    using std::u32streampos _USING_IF_EXISTS;
    using std::u8streampos _USING_IF_EXISTS;
    using std::wstreampos _USING_IF_EXISTS;
    using std::basic_osyncstream _USING_IF_EXISTS;
    using std::basic_syncbuf _USING_IF_EXISTS;
    using std::istreambuf_iterator _USING_IF_EXISTS;
    using std::ostreambuf_iterator _USING_IF_EXISTS;
    using std::osyncstream _USING_IF_EXISTS;
    using std::syncbuf _USING_IF_EXISTS;
    using std::wosyncstream _USING_IF_EXISTS;
    using std::wsyncbuf _USING_IF_EXISTS;
    using std::fpos _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::cerr _USING_IF_EXISTS;
    using std::cin _USING_IF_EXISTS;
    using std::clog _USING_IF_EXISTS;
    using std::cout _USING_IF_EXISTS;
    using std::wcerr _USING_IF_EXISTS;
    using std::wcin _USING_IF_EXISTS;
    using std::wclog _USING_IF_EXISTS;
    using std::wcout _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::basic_iostream _USING_IF_EXISTS;
    using std::basic_istream _USING_IF_EXISTS;
    using std::iostream _USING_IF_EXISTS;
    using std::istream _USING_IF_EXISTS;
    using std::wiostream _USING_IF_EXISTS;
    using std::wistream _USING_IF_EXISTS;
    using std::ws _USING_IF_EXISTS;
    using std::operator>> _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::incrementable_traits _USING_IF_EXISTS;
    using std::indirectly_readable_traits _USING_IF_EXISTS;
    using std::iter_difference_t _USING_IF_EXISTS;
    using std::iter_reference_t _USING_IF_EXISTS;
    using std::iter_value_t _USING_IF_EXISTS;
    using std::iterator_traits _USING_IF_EXISTS;
    namespace ranges
    {
        inline namespace _Cpo
        {
            using std::ranges::_Cpo::iter_move _USING_IF_EXISTS;
            using std::ranges::_Cpo::iter_swap _USING_IF_EXISTS;
        } // namespace _Cpo
    } // namespace ranges
    using std::advance _USING_IF_EXISTS;
    using std::bidirectional_iterator _USING_IF_EXISTS;
    using std::bidirectional_iterator_tag _USING_IF_EXISTS;
    using std::contiguous_iterator _USING_IF_EXISTS;
    using std::contiguous_iterator_tag _USING_IF_EXISTS;
    using std::disable_sized_sentinel_for _USING_IF_EXISTS;
    using std::distance _USING_IF_EXISTS;
    using std::forward_iterator _USING_IF_EXISTS;
    using std::forward_iterator_tag _USING_IF_EXISTS;
    using std::incrementable _USING_IF_EXISTS;
    using std::indirect_binary_predicate _USING_IF_EXISTS;
    using std::indirect_equivalence_relation _USING_IF_EXISTS;
    using std::indirect_result_t _USING_IF_EXISTS;
    using std::indirect_strict_weak_order _USING_IF_EXISTS;
    using std::indirect_unary_predicate _USING_IF_EXISTS;
    using std::indirectly_comparable _USING_IF_EXISTS;
    using std::indirectly_copyable _USING_IF_EXISTS;
    using std::indirectly_copyable_storable _USING_IF_EXISTS;
    using std::indirectly_movable _USING_IF_EXISTS;
    using std::indirectly_movable_storable _USING_IF_EXISTS;
    using std::indirectly_readable _USING_IF_EXISTS;
    using std::indirectly_regular_unary_invocable _USING_IF_EXISTS;
    using std::indirectly_swappable _USING_IF_EXISTS;
    using std::indirectly_unary_invocable _USING_IF_EXISTS;
    using std::indirectly_writable _USING_IF_EXISTS;
    using std::input_iterator _USING_IF_EXISTS;
    using std::input_iterator_tag _USING_IF_EXISTS;
    using std::input_or_output_iterator _USING_IF_EXISTS;
    using std::iter_common_reference_t _USING_IF_EXISTS;
    using std::iter_rvalue_reference_t _USING_IF_EXISTS;
    using std::mergeable _USING_IF_EXISTS;
    using std::next _USING_IF_EXISTS;
    using std::output_iterator _USING_IF_EXISTS;
    using std::output_iterator_tag _USING_IF_EXISTS;
    using std::permutable _USING_IF_EXISTS;
    using std::prev _USING_IF_EXISTS;
    using std::projected _USING_IF_EXISTS;
    using std::random_access_iterator _USING_IF_EXISTS;
    using std::random_access_iterator_tag _USING_IF_EXISTS;
    using std::sentinel_for _USING_IF_EXISTS;
    using std::sized_sentinel_for _USING_IF_EXISTS;
    using std::sortable _USING_IF_EXISTS;
    using std::weakly_incrementable _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::advance _USING_IF_EXISTS;
        using std::ranges::distance _USING_IF_EXISTS;
        using std::ranges::next _USING_IF_EXISTS;
        using std::ranges::prev _USING_IF_EXISTS;
    } // namespace ranges
    using std::reverse_iterator _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::operator- _USING_IF_EXISTS;
    using std::operator+ _USING_IF_EXISTS;
    using std::back_insert_iterator _USING_IF_EXISTS;
    using std::back_inserter _USING_IF_EXISTS;
    using std::begin _USING_IF_EXISTS;
    using std::cbegin _USING_IF_EXISTS;
    using std::cend _USING_IF_EXISTS;
    using std::common_iterator _USING_IF_EXISTS;
    using std::counted_iterator _USING_IF_EXISTS;
    using std::crbegin _USING_IF_EXISTS;
    using std::crend _USING_IF_EXISTS;
    using std::data _USING_IF_EXISTS;
    using std::default_sentinel _USING_IF_EXISTS;
    using std::default_sentinel_t _USING_IF_EXISTS;
    using std::empty _USING_IF_EXISTS;
    using std::end _USING_IF_EXISTS;
    using std::front_insert_iterator _USING_IF_EXISTS;
    using std::front_inserter _USING_IF_EXISTS;
    using std::insert_iterator _USING_IF_EXISTS;
    using std::inserter _USING_IF_EXISTS;
    using std::istream_iterator _USING_IF_EXISTS;
    using std::istreambuf_iterator _USING_IF_EXISTS;
    using std::iterator _USING_IF_EXISTS;
    using std::make_move_iterator _USING_IF_EXISTS;
    using std::make_reverse_iterator _USING_IF_EXISTS;
    using std::move_iterator _USING_IF_EXISTS;
    using std::move_sentinel _USING_IF_EXISTS;
    using std::ostream_iterator _USING_IF_EXISTS;
    using std::ostreambuf_iterator _USING_IF_EXISTS;
    using std::rbegin _USING_IF_EXISTS;
    using std::rend _USING_IF_EXISTS;
    using std::size _USING_IF_EXISTS;
    using std::ssize _USING_IF_EXISTS;
    using std::unreachable_sentinel _USING_IF_EXISTS;
    using std::unreachable_sentinel_t _USING_IF_EXISTS;
    using std::unreachable _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::latch _USING_IF_EXISTS;
}
export namespace std
{
    using std::float_denorm_style _USING_IF_EXISTS;
    using std::float_round_style _USING_IF_EXISTS;
    using std::numeric_limits _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::list _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::erase _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::list _USING_IF_EXISTS;
    }
} // namespace std

export namespace std
{
    using std::codecvt _USING_IF_EXISTS;
    using std::codecvt_base _USING_IF_EXISTS;
    using std::codecvt_byname _USING_IF_EXISTS;
    using std::collate _USING_IF_EXISTS;
    using std::collate_byname _USING_IF_EXISTS;
    using std::ctype _USING_IF_EXISTS;
    using std::ctype_base _USING_IF_EXISTS;
    using std::ctype_byname _USING_IF_EXISTS;
    using std::has_facet _USING_IF_EXISTS;
    using std::isalnum _USING_IF_EXISTS;
    using std::isalpha _USING_IF_EXISTS;
    using std::isblank _USING_IF_EXISTS;
    using std::iscntrl _USING_IF_EXISTS;
    using std::isdigit _USING_IF_EXISTS;
    using std::isgraph _USING_IF_EXISTS;
    using std::islower _USING_IF_EXISTS;
    using std::isprint _USING_IF_EXISTS;
    using std::ispunct _USING_IF_EXISTS;
    using std::isspace _USING_IF_EXISTS;
    using std::isupper _USING_IF_EXISTS;
    using std::isxdigit _USING_IF_EXISTS;
    using std::locale _USING_IF_EXISTS;
    using std::messages _USING_IF_EXISTS;
    using std::messages_base _USING_IF_EXISTS;
    using std::messages_byname _USING_IF_EXISTS;
    using std::money_base _USING_IF_EXISTS;
    using std::money_get _USING_IF_EXISTS;
    using std::money_put _USING_IF_EXISTS;
    using std::moneypunct _USING_IF_EXISTS;
    using std::moneypunct_byname _USING_IF_EXISTS;
    using std::num_get _USING_IF_EXISTS;
    using std::num_put _USING_IF_EXISTS;
    using std::numpunct _USING_IF_EXISTS;
    using std::numpunct_byname _USING_IF_EXISTS;
    using std::time_base _USING_IF_EXISTS;
    using std::time_get _USING_IF_EXISTS;
    using std::time_get_byname _USING_IF_EXISTS;
    using std::time_put _USING_IF_EXISTS;
    using std::time_put_byname _USING_IF_EXISTS;
    using std::tolower _USING_IF_EXISTS;
    using std::toupper _USING_IF_EXISTS;
    using std::use_facet _USING_IF_EXISTS;
    using std::wbuffer_convert _USING_IF_EXISTS;
    using std::wstring_convert _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::map _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::multimap _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::map _USING_IF_EXISTS;
        using std::pmr::multimap _USING_IF_EXISTS;
    } // namespace pmr
} // namespace std

export namespace std
{
    using std::align _USING_IF_EXISTS;
    using std::allocator _USING_IF_EXISTS;
    using std::allocator_arg _USING_IF_EXISTS;
    using std::allocator_arg_t _USING_IF_EXISTS;
    using std::allocator_traits _USING_IF_EXISTS;
    using std::assume_aligned _USING_IF_EXISTS;
    using std::make_obj_using_allocator _USING_IF_EXISTS;
    using std::pointer_traits _USING_IF_EXISTS;
    using std::to_address _USING_IF_EXISTS;
    using std::uninitialized_construct_using_allocator _USING_IF_EXISTS;
    using std::uses_allocator _USING_IF_EXISTS;
    using std::uses_allocator_construction_args _USING_IF_EXISTS;
    using std::uses_allocator_v _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::addressof _USING_IF_EXISTS;
    using std::uninitialized_default_construct _USING_IF_EXISTS;
    using std::uninitialized_default_construct_n _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::uninitialized_default_construct _USING_IF_EXISTS;
        using std::ranges::uninitialized_default_construct_n _USING_IF_EXISTS;
    } // namespace ranges
    using std::uninitialized_value_construct _USING_IF_EXISTS;
    using std::uninitialized_value_construct_n _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::uninitialized_value_construct _USING_IF_EXISTS;
        using std::ranges::uninitialized_value_construct_n _USING_IF_EXISTS;
    } // namespace ranges
    using std::uninitialized_copy _USING_IF_EXISTS;
    using std::uninitialized_copy_n _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::uninitialized_copy _USING_IF_EXISTS;
        using std::ranges::uninitialized_copy_n _USING_IF_EXISTS;
        using std::ranges::uninitialized_copy_n_result _USING_IF_EXISTS;
        using std::ranges::uninitialized_copy_result _USING_IF_EXISTS;
    } // namespace ranges
    using std::uninitialized_move _USING_IF_EXISTS;
    using std::uninitialized_move_n _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::uninitialized_move _USING_IF_EXISTS;
        using std::ranges::uninitialized_move_n _USING_IF_EXISTS;
        using std::ranges::uninitialized_move_n_result _USING_IF_EXISTS;
        using std::ranges::uninitialized_move_result _USING_IF_EXISTS;
    } // namespace ranges
    using std::uninitialized_fill _USING_IF_EXISTS;
    using std::uninitialized_fill_n _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::uninitialized_fill _USING_IF_EXISTS;
        using std::ranges::uninitialized_fill_n _USING_IF_EXISTS;
    } // namespace ranges
    using std::construct_at _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::construct_at _USING_IF_EXISTS;
    }
    using std::destroy _USING_IF_EXISTS;
    using std::destroy_at _USING_IF_EXISTS;
    using std::destroy_n _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::destroy _USING_IF_EXISTS;
        using std::ranges::destroy_at _USING_IF_EXISTS;
        using std::ranges::destroy_n _USING_IF_EXISTS;
        using std::ranges::elements_of _USING_IF_EXISTS;
    } // namespace ranges
    using std::default_delete _USING_IF_EXISTS;
    using std::make_unique _USING_IF_EXISTS;
    using std::make_unique_for_overwrite _USING_IF_EXISTS;
    using std::unique_ptr _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::allocate_shared _USING_IF_EXISTS;
    using std::allocate_shared_for_overwrite _USING_IF_EXISTS;
    using std::bad_weak_ptr _USING_IF_EXISTS;
    using std::const_pointer_cast _USING_IF_EXISTS;
    using std::dynamic_pointer_cast _USING_IF_EXISTS;
    using std::make_shared _USING_IF_EXISTS;
    using std::make_shared_for_overwrite _USING_IF_EXISTS;
    using std::reinterpret_pointer_cast _USING_IF_EXISTS;
    using std::shared_ptr _USING_IF_EXISTS;
    using std::static_pointer_cast _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::get_deleter _USING_IF_EXISTS;
    using std::atomic_compare_exchange_strong _USING_IF_EXISTS;
    using std::atomic_compare_exchange_strong_explicit _USING_IF_EXISTS;
    using std::atomic_compare_exchange_weak _USING_IF_EXISTS;
    using std::atomic_compare_exchange_weak_explicit _USING_IF_EXISTS;
    using std::atomic_exchange _USING_IF_EXISTS;
    using std::atomic_exchange_explicit _USING_IF_EXISTS;
    using std::atomic_is_lock_free _USING_IF_EXISTS;
    using std::atomic_load _USING_IF_EXISTS;
    using std::atomic_load_explicit _USING_IF_EXISTS;
    using std::atomic_store _USING_IF_EXISTS;
    using std::atomic_store_explicit _USING_IF_EXISTS;
    using std::enable_shared_from_this _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    using std::owner_less _USING_IF_EXISTS;
    using std::weak_ptr _USING_IF_EXISTS;
} // namespace std

export namespace std::pmr
{
    using std::pmr::memory_resource _USING_IF_EXISTS;
    using std::pmr::operator== _USING_IF_EXISTS;
    using std::pmr::get_default_resource _USING_IF_EXISTS;
    using std::pmr::monotonic_buffer_resource _USING_IF_EXISTS;
    using std::pmr::new_delete_resource _USING_IF_EXISTS;
    using std::pmr::null_memory_resource _USING_IF_EXISTS;
    using std::pmr::polymorphic_allocator _USING_IF_EXISTS;
    using std::pmr::pool_options _USING_IF_EXISTS;
    using std::pmr::set_default_resource _USING_IF_EXISTS;
    using std::pmr::synchronized_pool_resource _USING_IF_EXISTS;
    using std::pmr::unsynchronized_pool_resource _USING_IF_EXISTS;
} // namespace std::pmr
export namespace std
{
    using std::adopt_lock _USING_IF_EXISTS;
    using std::adopt_lock_t _USING_IF_EXISTS;
    using std::call_once _USING_IF_EXISTS;
    using std::defer_lock _USING_IF_EXISTS;
    using std::defer_lock_t _USING_IF_EXISTS;
    using std::lock _USING_IF_EXISTS;
    using std::lock_guard _USING_IF_EXISTS;
    using std::mutex _USING_IF_EXISTS;
    using std::once_flag _USING_IF_EXISTS;
    using std::recursive_mutex _USING_IF_EXISTS;
    using std::recursive_timed_mutex _USING_IF_EXISTS;
    using std::scoped_lock _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::timed_mutex _USING_IF_EXISTS;
    using std::try_lock _USING_IF_EXISTS;
    using std::try_to_lock _USING_IF_EXISTS;
    using std::try_to_lock_t _USING_IF_EXISTS;
    using std::unique_lock _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::align_val_t _USING_IF_EXISTS;
    using std::bad_alloc _USING_IF_EXISTS;
    using std::bad_array_new_length _USING_IF_EXISTS;
    using std::destroying_delete _USING_IF_EXISTS;
    using std::destroying_delete_t _USING_IF_EXISTS;
    using std::get_new_handler _USING_IF_EXISTS;
    using std::launder _USING_IF_EXISTS;
    using std::new_handler _USING_IF_EXISTS;
    using std::nothrow _USING_IF_EXISTS;
    using std::nothrow_t _USING_IF_EXISTS;
    using std::set_new_handler _USING_IF_EXISTS;
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
    using std::numbers::e _USING_IF_EXISTS;
    using std::numbers::e_v _USING_IF_EXISTS;
    using std::numbers::egamma _USING_IF_EXISTS;
    using std::numbers::egamma_v _USING_IF_EXISTS;
    using std::numbers::inv_pi _USING_IF_EXISTS;
    using std::numbers::inv_pi_v _USING_IF_EXISTS;
    using std::numbers::inv_sqrt3 _USING_IF_EXISTS;
    using std::numbers::inv_sqrt3_v _USING_IF_EXISTS;
    using std::numbers::inv_sqrtpi _USING_IF_EXISTS;
    using std::numbers::inv_sqrtpi_v _USING_IF_EXISTS;
    using std::numbers::ln10 _USING_IF_EXISTS;
    using std::numbers::ln10_v _USING_IF_EXISTS;
    using std::numbers::ln2 _USING_IF_EXISTS;
    using std::numbers::ln2_v _USING_IF_EXISTS;
    using std::numbers::log10e _USING_IF_EXISTS;
    using std::numbers::log10e_v _USING_IF_EXISTS;
    using std::numbers::log2e _USING_IF_EXISTS;
    using std::numbers::log2e_v _USING_IF_EXISTS;
    using std::numbers::phi _USING_IF_EXISTS;
    using std::numbers::phi_v _USING_IF_EXISTS;
    using std::numbers::pi _USING_IF_EXISTS;
    using std::numbers::pi_v _USING_IF_EXISTS;
    using std::numbers::sqrt2 _USING_IF_EXISTS;
    using std::numbers::sqrt2_v _USING_IF_EXISTS;
    using std::numbers::sqrt3 _USING_IF_EXISTS;
    using std::numbers::sqrt3_v _USING_IF_EXISTS;
} // namespace std::numbers

export namespace std
{
    using std::accumulate _USING_IF_EXISTS;
    using std::adjacent_difference _USING_IF_EXISTS;
    using std::exclusive_scan _USING_IF_EXISTS;
    using std::inclusive_scan _USING_IF_EXISTS;
    using std::inner_product _USING_IF_EXISTS;
    using std::iota _USING_IF_EXISTS;
    using std::partial_sum _USING_IF_EXISTS;
    using std::reduce _USING_IF_EXISTS;
    using std::transform_exclusive_scan _USING_IF_EXISTS;
    using std::transform_inclusive_scan _USING_IF_EXISTS;
    using std::transform_reduce _USING_IF_EXISTS;
    using std::gcd _USING_IF_EXISTS;
    using std::lcm _USING_IF_EXISTS;
    using std::midpoint _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::bad_optional_access _USING_IF_EXISTS;
    using std::nullopt _USING_IF_EXISTS;
    using std::nullopt_t _USING_IF_EXISTS;
    using std::optional _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    using std::make_optional _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::basic_ostream _USING_IF_EXISTS;
    using std::endl _USING_IF_EXISTS;
    using std::ends _USING_IF_EXISTS;
    using std::flush _USING_IF_EXISTS;
    using std::ostream _USING_IF_EXISTS;
    using std::wostream _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::queue _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::priority_queue _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::uses_allocator _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::bernoulli_distribution _USING_IF_EXISTS;
    using std::binomial_distribution _USING_IF_EXISTS;
    using std::cauchy_distribution _USING_IF_EXISTS;
    using std::chi_squared_distribution _USING_IF_EXISTS;
    using std::default_random_engine _USING_IF_EXISTS;
    using std::discard_block_engine _USING_IF_EXISTS;
    using std::discrete_distribution _USING_IF_EXISTS;
    using std::exponential_distribution _USING_IF_EXISTS;
    using std::extreme_value_distribution _USING_IF_EXISTS;
    using std::fisher_f_distribution _USING_IF_EXISTS;
    using std::gamma_distribution _USING_IF_EXISTS;
    using std::generate_canonical _USING_IF_EXISTS;
    using std::geometric_distribution _USING_IF_EXISTS;
    using std::independent_bits_engine _USING_IF_EXISTS;
    using std::knuth_b _USING_IF_EXISTS;
    using std::linear_congruential_engine _USING_IF_EXISTS;
    using std::lognormal_distribution _USING_IF_EXISTS;
    using std::mersenne_twister_engine _USING_IF_EXISTS;
    using std::minstd_rand _USING_IF_EXISTS;
    using std::minstd_rand0 _USING_IF_EXISTS;
    using std::mt19937 _USING_IF_EXISTS;
    using std::mt19937_64 _USING_IF_EXISTS;
    using std::negative_binomial_distribution _USING_IF_EXISTS;
    using std::normal_distribution _USING_IF_EXISTS;
    using std::piecewise_constant_distribution _USING_IF_EXISTS;
    using std::piecewise_linear_distribution _USING_IF_EXISTS;
    using std::poisson_distribution _USING_IF_EXISTS;
    using std::random_device _USING_IF_EXISTS;
    using std::ranlux24 _USING_IF_EXISTS;
    using std::ranlux24_base _USING_IF_EXISTS;
    using std::ranlux48 _USING_IF_EXISTS;
    using std::ranlux48_base _USING_IF_EXISTS;
    using std::seed_seq _USING_IF_EXISTS;
    using std::shuffle_order_engine _USING_IF_EXISTS;
    using std::student_t_distribution _USING_IF_EXISTS;
    using std::subtract_with_carry_engine _USING_IF_EXISTS;
    using std::uniform_int_distribution _USING_IF_EXISTS;
    using std::uniform_random_bit_generator _USING_IF_EXISTS;
    using std::uniform_real_distribution _USING_IF_EXISTS;
    using std::weibull_distribution _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    namespace ranges
    {
        inline namespace _Cpo
        {
            using std::ranges::_Cpo::begin _USING_IF_EXISTS;
            using std::ranges::_Cpo::cbegin _USING_IF_EXISTS;
            using std::ranges::_Cpo::cdata _USING_IF_EXISTS;
            using std::ranges::_Cpo::cend _USING_IF_EXISTS;
            using std::ranges::_Cpo::crbegin _USING_IF_EXISTS;
            using std::ranges::_Cpo::crend _USING_IF_EXISTS;
            using std::ranges::_Cpo::data _USING_IF_EXISTS;
            using std::ranges::_Cpo::empty _USING_IF_EXISTS;
            using std::ranges::_Cpo::end _USING_IF_EXISTS;
            using std::ranges::_Cpo::rbegin _USING_IF_EXISTS;
            using std::ranges::_Cpo::rend _USING_IF_EXISTS;
            using std::ranges::_Cpo::size _USING_IF_EXISTS;
            using std::ranges::_Cpo::ssize _USING_IF_EXISTS;
        } // namespace _Cpo
        using std::ranges::bidirectional_range _USING_IF_EXISTS;
        using std::ranges::borrowed_range _USING_IF_EXISTS;
        using std::ranges::common_range _USING_IF_EXISTS;
        using std::ranges::contiguous_range _USING_IF_EXISTS;
        using std::ranges::disable_sized_range _USING_IF_EXISTS;
        using std::ranges::enable_borrowed_range _USING_IF_EXISTS;
        using std::ranges::enable_view _USING_IF_EXISTS;
        using std::ranges::forward_range _USING_IF_EXISTS;
        using std::ranges::get _USING_IF_EXISTS;
        using std::ranges::input_range _USING_IF_EXISTS;
        using std::ranges::iterator_t _USING_IF_EXISTS;
        using std::ranges::output_range _USING_IF_EXISTS;
        using std::ranges::random_access_range _USING_IF_EXISTS;
        using std::ranges::range _USING_IF_EXISTS;
        // using std::ranges::range_common_reference_t _USING_IF_EXISTS; // not-implemented?
        using std::ranges::range_difference_t _USING_IF_EXISTS;
        using std::ranges::range_reference_t _USING_IF_EXISTS;
        using std::ranges::range_rvalue_reference_t _USING_IF_EXISTS;
        using std::ranges::range_size_t _USING_IF_EXISTS;
        using std::ranges::range_value_t _USING_IF_EXISTS;
        using std::ranges::sentinel_t _USING_IF_EXISTS;
        using std::ranges::sized_range _USING_IF_EXISTS;
        using std::ranges::subrange _USING_IF_EXISTS;
        using std::ranges::subrange_kind _USING_IF_EXISTS;
        using std::ranges::view _USING_IF_EXISTS;
        using std::ranges::view_base _USING_IF_EXISTS;
        using std::ranges::view_interface _USING_IF_EXISTS;
        using std::ranges::viewable_range _USING_IF_EXISTS;
    } // namespace ranges
    using std::ranges::get _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::borrowed_iterator_t _USING_IF_EXISTS;
        using std::ranges::borrowed_subrange_t _USING_IF_EXISTS;
        using std::ranges::dangling _USING_IF_EXISTS;
        using std::ranges::empty_view _USING_IF_EXISTS;
        using std::ranges::single_view _USING_IF_EXISTS;
        using std::ranges::iota_view _USING_IF_EXISTS;
        using std::ranges::basic_istream_view _USING_IF_EXISTS;
        using std::ranges::istream_view _USING_IF_EXISTS;
        using std::ranges::wistream_view _USING_IF_EXISTS;
        namespace views
        {
            using std::ranges::views::empty _USING_IF_EXISTS;
            using std::ranges::views::single _USING_IF_EXISTS;
            using std::ranges::views::iota _USING_IF_EXISTS;
            using std::ranges::views::repeat _USING_IF_EXISTS;
            using std::ranges::views::istream _USING_IF_EXISTS;
            using std::ranges::views::all _USING_IF_EXISTS;
            using std::ranges::views::all_t _USING_IF_EXISTS;
        } // namespace views

        using std::ranges::filter_view _USING_IF_EXISTS;
        using std::ranges::owning_view _USING_IF_EXISTS;
        using std::ranges::ref_view _USING_IF_EXISTS;
        using std::ranges::transform_view _USING_IF_EXISTS;
        using std::ranges::take_view _USING_IF_EXISTS;
        using std::ranges::take_while_view _USING_IF_EXISTS;
        using std::ranges::drop_view _USING_IF_EXISTS;
        using std::ranges::drop_while_view _USING_IF_EXISTS;
        using std::ranges::join_view _USING_IF_EXISTS;
        using std::ranges::lazy_split_view _USING_IF_EXISTS;
        using std::ranges::split_view _USING_IF_EXISTS;
        using std::ranges::common_view _USING_IF_EXISTS;
        using std::ranges::reverse_view _USING_IF_EXISTS;
        using std::ranges::elements_view _USING_IF_EXISTS;
        using std::ranges::keys_view _USING_IF_EXISTS;
        using std::ranges::values_view _USING_IF_EXISTS;
        namespace views
        {
            using std::ranges::views::filter _USING_IF_EXISTS;
            using std::ranges::views::transform _USING_IF_EXISTS;
            using std::ranges::views::take _USING_IF_EXISTS;
            using std::ranges::views::take_while _USING_IF_EXISTS;
            using std::ranges::views::drop _USING_IF_EXISTS;
            using std::ranges::views::drop_while _USING_IF_EXISTS;
            using std::ranges::views::join _USING_IF_EXISTS;
            using std::ranges::views::lazy_split _USING_IF_EXISTS;
            using std::ranges::views::split _USING_IF_EXISTS;
            using std::ranges::views::counted _USING_IF_EXISTS;
            using std::ranges::views::common _USING_IF_EXISTS;
            using std::ranges::views::reverse _USING_IF_EXISTS;
            using std::ranges::views::elements _USING_IF_EXISTS;
            using std::ranges::views::keys _USING_IF_EXISTS;
            using std::ranges::views::values _USING_IF_EXISTS;
        } // namespace views
    } // namespace ranges
    namespace views = ranges::views;
    using std::tuple_element _USING_IF_EXISTS;
    using std::tuple_size _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::atto _USING_IF_EXISTS;
    using std::centi _USING_IF_EXISTS;
    using std::deca _USING_IF_EXISTS;
    using std::deci _USING_IF_EXISTS;
    using std::exa _USING_IF_EXISTS;
    using std::femto _USING_IF_EXISTS;
    using std::giga _USING_IF_EXISTS;
    using std::hecto _USING_IF_EXISTS;
    using std::kilo _USING_IF_EXISTS;
    using std::mega _USING_IF_EXISTS;
    using std::micro _USING_IF_EXISTS;
    using std::milli _USING_IF_EXISTS;
    using std::nano _USING_IF_EXISTS;
    using std::peta _USING_IF_EXISTS;
    using std::pico _USING_IF_EXISTS;
    using std::ratio _USING_IF_EXISTS;
    using std::ratio_add _USING_IF_EXISTS;
    using std::ratio_divide _USING_IF_EXISTS;
    using std::ratio_equal _USING_IF_EXISTS;
    using std::ratio_equal_v _USING_IF_EXISTS;
    using std::ratio_greater _USING_IF_EXISTS;
    using std::ratio_greater_equal _USING_IF_EXISTS;
    using std::ratio_greater_equal_v _USING_IF_EXISTS;
    using std::ratio_greater_v _USING_IF_EXISTS;
    using std::ratio_less _USING_IF_EXISTS;
    using std::ratio_less_equal _USING_IF_EXISTS;
    using std::ratio_less_equal_v _USING_IF_EXISTS;
    using std::ratio_less_v _USING_IF_EXISTS;
    using std::ratio_multiply _USING_IF_EXISTS;
    using std::ratio_not_equal _USING_IF_EXISTS;
    using std::ratio_not_equal_v _USING_IF_EXISTS;
    using std::ratio_subtract _USING_IF_EXISTS;
    using std::tera _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    namespace regex_constants
    {
        using std::regex_constants::error_type _USING_IF_EXISTS;
        using std::regex_constants::match_flag_type _USING_IF_EXISTS;
        using std::regex_constants::syntax_option_type _USING_IF_EXISTS;
        using std::regex_constants::operator& _USING_IF_EXISTS;
        using std::regex_constants::operator&= _USING_IF_EXISTS;
        using std::regex_constants::operator^ _USING_IF_EXISTS;
        using std::regex_constants::operator^= _USING_IF_EXISTS;
        using std::regex_constants::operator| _USING_IF_EXISTS;
        using std::regex_constants::operator|= _USING_IF_EXISTS;
        using std::regex_constants::operator~ _USING_IF_EXISTS;
    } // namespace regex_constants
    using std::basic_regex _USING_IF_EXISTS;
    using std::csub_match _USING_IF_EXISTS;
    using std::regex _USING_IF_EXISTS;
    using std::regex_error _USING_IF_EXISTS;
    using std::regex_traits _USING_IF_EXISTS;
    using std::ssub_match _USING_IF_EXISTS;
    using std::sub_match _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::wcsub_match _USING_IF_EXISTS;
    using std::wregex _USING_IF_EXISTS;
    using std::wssub_match _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::cmatch _USING_IF_EXISTS;
    using std::cregex_iterator _USING_IF_EXISTS;
    using std::cregex_token_iterator _USING_IF_EXISTS;
    using std::match_results _USING_IF_EXISTS;
    using std::regex_iterator _USING_IF_EXISTS;
    using std::regex_match _USING_IF_EXISTS;
    using std::regex_replace _USING_IF_EXISTS;
    using std::regex_search _USING_IF_EXISTS;
    using std::regex_token_iterator _USING_IF_EXISTS;
    using std::smatch _USING_IF_EXISTS;
    using std::sregex_iterator _USING_IF_EXISTS;
    using std::sregex_token_iterator _USING_IF_EXISTS;
    using std::wcmatch _USING_IF_EXISTS;
    using std::wcregex_iterator _USING_IF_EXISTS;
    using std::wcregex_token_iterator _USING_IF_EXISTS;
    using std::wsmatch _USING_IF_EXISTS;
    using std::wsregex_iterator _USING_IF_EXISTS;
    using std::wsregex_token_iterator _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::cmatch _USING_IF_EXISTS;
        using std::pmr::match_results _USING_IF_EXISTS;
        using std::pmr::smatch _USING_IF_EXISTS;
        using std::pmr::wcmatch _USING_IF_EXISTS;
        using std::pmr::wsmatch _USING_IF_EXISTS;
    } // namespace pmr
} // namespace std

export namespace std
{
    using std::scoped_allocator_adaptor _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::binary_semaphore _USING_IF_EXISTS;
    using std::counting_semaphore _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::set _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::multiset _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::multiset _USING_IF_EXISTS;
        using std::pmr::set _USING_IF_EXISTS;
    } // namespace pmr
} // namespace std

export namespace std
{
    using std::shared_lock _USING_IF_EXISTS;
    using std::shared_mutex _USING_IF_EXISTS;
    using std::shared_timed_mutex _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::source_location _USING_IF_EXISTS;
}
export namespace std
{
    using std::dynamic_extent _USING_IF_EXISTS;
    using std::span _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::enable_borrowed_range _USING_IF_EXISTS;
        using std::ranges::enable_view _USING_IF_EXISTS;
    } // namespace ranges
    using std::as_bytes _USING_IF_EXISTS;
    using std::as_writable_bytes _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::basic_istringstream _USING_IF_EXISTS;
    using std::basic_ostringstream _USING_IF_EXISTS;
    using std::basic_stringbuf _USING_IF_EXISTS;
    using std::basic_stringstream _USING_IF_EXISTS;
    using std::istringstream _USING_IF_EXISTS;
    using std::ostringstream _USING_IF_EXISTS;
    using std::stringbuf _USING_IF_EXISTS;
    using std::stringstream _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::wistringstream _USING_IF_EXISTS;
    using std::wostringstream _USING_IF_EXISTS;
    using std::wstringbuf _USING_IF_EXISTS;
    using std::wstringstream _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::stack _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::uses_allocator _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::domain_error _USING_IF_EXISTS;
    using std::invalid_argument _USING_IF_EXISTS;
    using std::length_error _USING_IF_EXISTS;
    using std::logic_error _USING_IF_EXISTS;
    using std::out_of_range _USING_IF_EXISTS;
    using std::overflow_error _USING_IF_EXISTS;
    using std::range_error _USING_IF_EXISTS;
    using std::runtime_error _USING_IF_EXISTS;
    using std::underflow_error _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::basic_streambuf _USING_IF_EXISTS;
    using std::streambuf _USING_IF_EXISTS;
    using std::wstreambuf _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::basic_string _USING_IF_EXISTS;
    using std::char_traits _USING_IF_EXISTS;
    using std::operator+ _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::operator>> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::erase _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::getline _USING_IF_EXISTS;
    using std::stod _USING_IF_EXISTS;
    using std::stof _USING_IF_EXISTS;
    using std::stoi _USING_IF_EXISTS;
    using std::stol _USING_IF_EXISTS;
    using std::stold _USING_IF_EXISTS;
    using std::stoll _USING_IF_EXISTS;
    using std::stoul _USING_IF_EXISTS;
    using std::stoull _USING_IF_EXISTS;
    using std::string _USING_IF_EXISTS;
    using std::to_string _USING_IF_EXISTS;
    using std::to_wstring _USING_IF_EXISTS;
    using std::u16string _USING_IF_EXISTS;
    using std::u32string _USING_IF_EXISTS;
    using std::u8string _USING_IF_EXISTS;
    using std::wstring _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::basic_string _USING_IF_EXISTS;
        using std::pmr::string _USING_IF_EXISTS;
        using std::pmr::u16string _USING_IF_EXISTS;
        using std::pmr::u32string _USING_IF_EXISTS;
        using std::pmr::u8string _USING_IF_EXISTS;
        using std::pmr::wstring _USING_IF_EXISTS;
    } // namespace pmr
    using std::hash _USING_IF_EXISTS;
    inline namespace literals
    {
        inline namespace string_literals
        {
            using std::literals::string_literals::operator""s _USING_IF_EXISTS;
        }
    } // namespace literals
} // namespace std

export namespace std
{
    using std::basic_string_view _USING_IF_EXISTS;
    namespace ranges
    {
        using std::ranges::enable_borrowed_range _USING_IF_EXISTS;
        using std::ranges::enable_view _USING_IF_EXISTS;
    } // namespace ranges
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    using std::string_view _USING_IF_EXISTS;
    using std::u16string_view _USING_IF_EXISTS;
    using std::u32string_view _USING_IF_EXISTS;
    using std::u8string_view _USING_IF_EXISTS;
    using std::wstring_view _USING_IF_EXISTS;
    inline namespace literals
    {
        inline namespace string_view_literals
        {
            using std::literals::string_view_literals::operator""sv _USING_IF_EXISTS;
        }
    } // namespace literals
} // namespace std

export namespace std
{
    using std::istrstream _USING_IF_EXISTS;
    using std::ostrstream _USING_IF_EXISTS;
    using std::strstream _USING_IF_EXISTS;
    using std::strstreambuf _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::basic_syncbuf _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::basic_osyncstream _USING_IF_EXISTS;
    using std::osyncstream _USING_IF_EXISTS;
    using std::syncbuf _USING_IF_EXISTS;
    using std::wosyncstream _USING_IF_EXISTS;
    using std::wsyncbuf _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::errc _USING_IF_EXISTS;
    using std::error_category _USING_IF_EXISTS;
    using std::error_code _USING_IF_EXISTS;
    using std::error_condition _USING_IF_EXISTS;
    using std::generic_category _USING_IF_EXISTS;
    using std::is_error_code_enum _USING_IF_EXISTS;
    using std::is_error_condition_enum _USING_IF_EXISTS;
    // using std::make_error_code _USING_IF_EXISTS;
    using std::system_category _USING_IF_EXISTS;
    using std::system_error _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    // using std::make_error_condition _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    using std::is_error_code_enum_v _USING_IF_EXISTS;
    using std::is_error_condition_enum_v _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::swap _USING_IF_EXISTS;
    using std::thread _USING_IF_EXISTS;
    using std::jthread _USING_IF_EXISTS;
    namespace this_thread
    {
        using std::this_thread::get_id _USING_IF_EXISTS;
        using std::this_thread::sleep_for _USING_IF_EXISTS;
        using std::this_thread::sleep_until _USING_IF_EXISTS;
        using std::this_thread::yield _USING_IF_EXISTS;
    } // namespace this_thread
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::apply _USING_IF_EXISTS;
    using std::forward_as_tuple _USING_IF_EXISTS;
    using std::get _USING_IF_EXISTS;
    using std::ignore _USING_IF_EXISTS;
    using std::make_from_tuple _USING_IF_EXISTS;
    using std::make_tuple _USING_IF_EXISTS;
    using std::tie _USING_IF_EXISTS;
    using std::tuple _USING_IF_EXISTS;
    using std::_Tuple_impl _USING_IF_EXISTS;
    using std::tuple_cat _USING_IF_EXISTS;
    using std::tuple_element _USING_IF_EXISTS;
    using std::tuple_element_t _USING_IF_EXISTS;
    using std::tuple_size _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::tuple_size_v _USING_IF_EXISTS;
    using std::uses_allocator _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::add_const _USING_IF_EXISTS;
    using std::add_const_t _USING_IF_EXISTS;
    using std::add_cv _USING_IF_EXISTS;
    using std::add_cv_t _USING_IF_EXISTS;
    using std::add_lvalue_reference _USING_IF_EXISTS;
    using std::add_lvalue_reference_t _USING_IF_EXISTS;
    using std::add_pointer _USING_IF_EXISTS;
    using std::add_pointer_t _USING_IF_EXISTS;
    using std::add_rvalue_reference _USING_IF_EXISTS;
    using std::add_rvalue_reference_t _USING_IF_EXISTS;
    using std::add_volatile _USING_IF_EXISTS;
    using std::add_volatile_t _USING_IF_EXISTS;
    using std::aligned_storage _USING_IF_EXISTS;
    using std::aligned_storage_t _USING_IF_EXISTS;
    using std::aligned_union _USING_IF_EXISTS;
    using std::aligned_union_t _USING_IF_EXISTS;
    using std::alignment_of _USING_IF_EXISTS;
    using std::alignment_of_v _USING_IF_EXISTS;
    using std::basic_common_reference _USING_IF_EXISTS;
    using std::bool_constant _USING_IF_EXISTS;
    using std::common_reference _USING_IF_EXISTS;
    using std::common_reference_t _USING_IF_EXISTS;
    using std::common_type _USING_IF_EXISTS;
    using std::common_type_t _USING_IF_EXISTS;
    using std::conditional _USING_IF_EXISTS;
    using std::conditional_t _USING_IF_EXISTS;
    using std::conjunction _USING_IF_EXISTS;
    using std::conjunction_v _USING_IF_EXISTS;
    using std::decay _USING_IF_EXISTS;
    using std::decay_t _USING_IF_EXISTS;
    using std::disjunction _USING_IF_EXISTS;
    using std::disjunction_v _USING_IF_EXISTS;
    using std::enable_if _USING_IF_EXISTS;
    using std::enable_if_t _USING_IF_EXISTS;
    using std::extent _USING_IF_EXISTS;
    using std::extent_v _USING_IF_EXISTS;
    using std::false_type _USING_IF_EXISTS;
    using std::has_unique_object_representations _USING_IF_EXISTS;
    using std::has_unique_object_representations_v _USING_IF_EXISTS;
    using std::has_virtual_destructor _USING_IF_EXISTS;
    using std::has_virtual_destructor_v _USING_IF_EXISTS;
    using std::integral_constant _USING_IF_EXISTS;
    using std::invoke_result _USING_IF_EXISTS;
    using std::invoke_result_t _USING_IF_EXISTS;
    using std::is_abstract _USING_IF_EXISTS;
    using std::is_abstract_v _USING_IF_EXISTS;
    using std::is_aggregate _USING_IF_EXISTS;
    using std::is_aggregate_v _USING_IF_EXISTS;
    using std::is_arithmetic _USING_IF_EXISTS;
    using std::is_arithmetic_v _USING_IF_EXISTS;
    using std::is_array _USING_IF_EXISTS;
    using std::is_array_v _USING_IF_EXISTS;
    using std::is_assignable _USING_IF_EXISTS;
    using std::is_assignable_v _USING_IF_EXISTS;
    using std::is_base_of _USING_IF_EXISTS;
    using std::is_base_of_v _USING_IF_EXISTS;
    using std::is_bounded_array _USING_IF_EXISTS;
    using std::is_bounded_array_v _USING_IF_EXISTS;
    using std::is_class _USING_IF_EXISTS;
    using std::is_class_v _USING_IF_EXISTS;
    using std::is_compound _USING_IF_EXISTS;
    using std::is_compound_v _USING_IF_EXISTS;
    using std::is_const _USING_IF_EXISTS;
    using std::is_const_v _USING_IF_EXISTS;
    using std::is_constant_evaluated _USING_IF_EXISTS;
    using std::is_constructible _USING_IF_EXISTS;
    using std::is_constructible_v _USING_IF_EXISTS;
    using std::is_convertible _USING_IF_EXISTS;
    using std::is_convertible_v _USING_IF_EXISTS;
    using std::is_copy_assignable _USING_IF_EXISTS;
    using std::is_copy_assignable_v _USING_IF_EXISTS;
    using std::is_copy_constructible _USING_IF_EXISTS;
    using std::is_copy_constructible_v _USING_IF_EXISTS;
    using std::is_default_constructible _USING_IF_EXISTS;
    using std::is_default_constructible_v _USING_IF_EXISTS;
    using std::is_destructible _USING_IF_EXISTS;
    using std::is_destructible_v _USING_IF_EXISTS;
    using std::is_empty _USING_IF_EXISTS;
    using std::is_empty_v _USING_IF_EXISTS;
    using std::is_enum _USING_IF_EXISTS;
    using std::is_enum_v _USING_IF_EXISTS;
    using std::is_final _USING_IF_EXISTS;
    using std::is_final_v _USING_IF_EXISTS;
    using std::is_floating_point _USING_IF_EXISTS;
    using std::is_floating_point_v _USING_IF_EXISTS;
    using std::is_function _USING_IF_EXISTS;
    using std::is_function_v _USING_IF_EXISTS;
    using std::is_fundamental _USING_IF_EXISTS;
    using std::is_fundamental_v _USING_IF_EXISTS;
    using std::is_integral _USING_IF_EXISTS;
    using std::is_integral_v _USING_IF_EXISTS;
    using std::is_invocable _USING_IF_EXISTS;
    using std::is_invocable_r _USING_IF_EXISTS;
    using std::is_invocable_r_v _USING_IF_EXISTS;
    using std::is_invocable_v _USING_IF_EXISTS;
    using std::is_lvalue_reference _USING_IF_EXISTS;
    using std::is_lvalue_reference_v _USING_IF_EXISTS;
    using std::is_member_function_pointer _USING_IF_EXISTS;
    using std::is_member_function_pointer_v _USING_IF_EXISTS;
    using std::is_member_object_pointer _USING_IF_EXISTS;
    using std::is_member_object_pointer_v _USING_IF_EXISTS;
    using std::is_member_pointer _USING_IF_EXISTS;
    using std::is_member_pointer_v _USING_IF_EXISTS;
    using std::is_move_assignable _USING_IF_EXISTS;
    using std::is_move_assignable_v _USING_IF_EXISTS;
    using std::is_move_constructible _USING_IF_EXISTS;
    using std::is_move_constructible_v _USING_IF_EXISTS;
    using std::is_nothrow_assignable _USING_IF_EXISTS;
    using std::is_nothrow_assignable_v _USING_IF_EXISTS;
    using std::is_nothrow_constructible _USING_IF_EXISTS;
    using std::is_nothrow_constructible_v _USING_IF_EXISTS;
    using std::is_nothrow_convertible _USING_IF_EXISTS;
    using std::is_nothrow_convertible_v _USING_IF_EXISTS;
    using std::is_nothrow_copy_assignable _USING_IF_EXISTS;
    using std::is_nothrow_copy_assignable_v _USING_IF_EXISTS;
    using std::is_nothrow_copy_constructible _USING_IF_EXISTS;
    using std::is_nothrow_copy_constructible_v _USING_IF_EXISTS;
    using std::is_nothrow_default_constructible _USING_IF_EXISTS;
    using std::is_nothrow_default_constructible_v _USING_IF_EXISTS;
    using std::is_nothrow_destructible _USING_IF_EXISTS;
    using std::is_nothrow_destructible_v _USING_IF_EXISTS;
    using std::is_nothrow_invocable _USING_IF_EXISTS;
    using std::is_nothrow_invocable_r _USING_IF_EXISTS;
    using std::is_nothrow_invocable_r_v _USING_IF_EXISTS;
    using std::is_nothrow_invocable_v _USING_IF_EXISTS;
    using std::is_nothrow_move_assignable _USING_IF_EXISTS;
    using std::is_nothrow_move_assignable_v _USING_IF_EXISTS;
    using std::is_nothrow_move_constructible _USING_IF_EXISTS;
    using std::is_nothrow_move_constructible_v _USING_IF_EXISTS;
    using std::is_nothrow_swappable _USING_IF_EXISTS;
    using std::is_nothrow_swappable_v _USING_IF_EXISTS;
    using std::is_nothrow_swappable_with _USING_IF_EXISTS;
    using std::is_nothrow_swappable_with_v _USING_IF_EXISTS;
    using std::is_null_pointer _USING_IF_EXISTS;
    using std::is_null_pointer_v _USING_IF_EXISTS;
    using std::is_object _USING_IF_EXISTS;
    using std::is_object_v _USING_IF_EXISTS;
    using std::is_pod _USING_IF_EXISTS;
    using std::is_pod_v _USING_IF_EXISTS;
    using std::is_pointer _USING_IF_EXISTS;
    using std::is_pointer_v _USING_IF_EXISTS;
    using std::is_polymorphic _USING_IF_EXISTS;
    using std::is_polymorphic_v _USING_IF_EXISTS;
    using std::is_reference _USING_IF_EXISTS;
    using std::is_reference_v _USING_IF_EXISTS;
    using std::is_rvalue_reference _USING_IF_EXISTS;
    using std::is_rvalue_reference_v _USING_IF_EXISTS;
    using std::is_same _USING_IF_EXISTS;
    using std::is_same_v _USING_IF_EXISTS;
    using std::is_scalar _USING_IF_EXISTS;
    using std::is_scalar_v _USING_IF_EXISTS;
    using std::is_signed _USING_IF_EXISTS;
    using std::is_signed_v _USING_IF_EXISTS;
    using std::is_standard_layout _USING_IF_EXISTS;
    using std::is_standard_layout_v _USING_IF_EXISTS;
    using std::is_swappable _USING_IF_EXISTS;
    using std::is_swappable_v _USING_IF_EXISTS;
    using std::is_swappable_with _USING_IF_EXISTS;
    using std::is_swappable_with_v _USING_IF_EXISTS;
    using std::is_trivial _USING_IF_EXISTS;
    using std::is_trivial_v _USING_IF_EXISTS;
    using std::is_trivially_assignable _USING_IF_EXISTS;
    using std::is_trivially_assignable_v _USING_IF_EXISTS;
    using std::is_trivially_constructible _USING_IF_EXISTS;
    using std::is_trivially_constructible_v _USING_IF_EXISTS;
    using std::is_trivially_copy_assignable _USING_IF_EXISTS;
    using std::is_trivially_copy_assignable_v _USING_IF_EXISTS;
    using std::is_trivially_copy_constructible _USING_IF_EXISTS;
    using std::is_trivially_copy_constructible_v _USING_IF_EXISTS;
    using std::is_trivially_copyable _USING_IF_EXISTS;
    using std::is_trivially_copyable_v _USING_IF_EXISTS;
    using std::is_trivially_default_constructible _USING_IF_EXISTS;
    using std::is_trivially_default_constructible_v _USING_IF_EXISTS;
    using std::is_trivially_destructible _USING_IF_EXISTS;
    using std::is_trivially_destructible_v _USING_IF_EXISTS;
    using std::is_trivially_move_assignable _USING_IF_EXISTS;
    using std::is_trivially_move_assignable_v _USING_IF_EXISTS;
    using std::is_trivially_move_constructible _USING_IF_EXISTS;
    using std::is_trivially_move_constructible_v _USING_IF_EXISTS;
    using std::is_unbounded_array _USING_IF_EXISTS;
    using std::is_unbounded_array_v _USING_IF_EXISTS;
    using std::is_union _USING_IF_EXISTS;
    using std::is_union_v _USING_IF_EXISTS;
    using std::is_unsigned _USING_IF_EXISTS;
    using std::is_unsigned_v _USING_IF_EXISTS;
    using std::is_void _USING_IF_EXISTS;
    using std::is_void_v _USING_IF_EXISTS;
    using std::is_volatile _USING_IF_EXISTS;
    using std::is_volatile_v _USING_IF_EXISTS;
    using std::make_signed _USING_IF_EXISTS;
    using std::make_signed_t _USING_IF_EXISTS;
    using std::make_unsigned _USING_IF_EXISTS;
    using std::make_unsigned_t _USING_IF_EXISTS;
    using std::negation _USING_IF_EXISTS;
    using std::negation_v _USING_IF_EXISTS;
    using std::rank _USING_IF_EXISTS;
    using std::rank_v _USING_IF_EXISTS;
    using std::remove_all_extents _USING_IF_EXISTS;
    using std::remove_all_extents_t _USING_IF_EXISTS;
    using std::remove_const _USING_IF_EXISTS;
    using std::remove_const_t _USING_IF_EXISTS;
    using std::remove_cv _USING_IF_EXISTS;
    using std::remove_cv_t _USING_IF_EXISTS;
    using std::remove_cvref _USING_IF_EXISTS;
    using std::remove_cvref_t _USING_IF_EXISTS;
    using std::remove_extent _USING_IF_EXISTS;
    using std::remove_extent_t _USING_IF_EXISTS;
    using std::remove_pointer _USING_IF_EXISTS;
    using std::remove_pointer_t _USING_IF_EXISTS;
    using std::remove_reference _USING_IF_EXISTS;
    using std::remove_reference_t _USING_IF_EXISTS;
    using std::remove_volatile _USING_IF_EXISTS;
    using std::remove_volatile_t _USING_IF_EXISTS;
    using std::true_type _USING_IF_EXISTS;
    using std::type_identity _USING_IF_EXISTS;
    using std::type_identity_t _USING_IF_EXISTS;
    using std::underlying_type _USING_IF_EXISTS;
    using std::underlying_type_t _USING_IF_EXISTS;
    using std::unwrap_ref_decay _USING_IF_EXISTS;
    using std::unwrap_ref_decay_t _USING_IF_EXISTS;
    using std::unwrap_reference _USING_IF_EXISTS;
    using std::unwrap_reference_t _USING_IF_EXISTS;
    using std::void_t _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::hash _USING_IF_EXISTS;
    using std::type_index _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::bad_cast _USING_IF_EXISTS;
    using std::bad_typeid _USING_IF_EXISTS;
    using std::type_info _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::unordered_map _USING_IF_EXISTS;
    using std::unordered_multimap _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::unordered_map _USING_IF_EXISTS;
        using std::pmr::unordered_multimap _USING_IF_EXISTS;
    } // namespace pmr
} // namespace std

export namespace std
{
    using std::unordered_multiset _USING_IF_EXISTS;
    using std::unordered_set _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::unordered_multiset _USING_IF_EXISTS;
        using std::pmr::unordered_set _USING_IF_EXISTS;
    } // namespace pmr
} // namespace std

export namespace std
{
    using std::as_const _USING_IF_EXISTS;
    using std::cmp_equal _USING_IF_EXISTS;
    using std::cmp_greater _USING_IF_EXISTS;
    using std::cmp_greater_equal _USING_IF_EXISTS;
    using std::cmp_less _USING_IF_EXISTS;
    using std::cmp_less_equal _USING_IF_EXISTS;
    using std::cmp_not_equal _USING_IF_EXISTS;
    using std::declval _USING_IF_EXISTS;
    using std::exchange _USING_IF_EXISTS;
    using std::forward _USING_IF_EXISTS;
    using std::in_range _USING_IF_EXISTS;
    using std::index_sequence _USING_IF_EXISTS;
    using std::index_sequence_for _USING_IF_EXISTS;
    using std::integer_sequence _USING_IF_EXISTS;
    using std::make_index_sequence _USING_IF_EXISTS;
    using std::make_integer_sequence _USING_IF_EXISTS;
    using std::move _USING_IF_EXISTS;
    using std::move_if_noexcept _USING_IF_EXISTS;
    using std::pair _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::get _USING_IF_EXISTS;
    using std::in_place _USING_IF_EXISTS;
    using std::in_place_index _USING_IF_EXISTS;
    using std::in_place_index_t _USING_IF_EXISTS;
    using std::in_place_t _USING_IF_EXISTS;
    using std::in_place_type _USING_IF_EXISTS;
    using std::in_place_type_t _USING_IF_EXISTS;
    using std::make_pair _USING_IF_EXISTS;
    using std::piecewise_construct _USING_IF_EXISTS;
    using std::piecewise_construct_t _USING_IF_EXISTS;
    using std::tuple_element _USING_IF_EXISTS;
    using std::tuple_size _USING_IF_EXISTS;
    using std::to_underlying _USING_IF_EXISTS;
    namespace rel_ops
    {
        using rel_ops::operator!= _USING_IF_EXISTS;
        using rel_ops::operator> _USING_IF_EXISTS;
        using rel_ops::operator<= _USING_IF_EXISTS;
        using rel_ops::operator>= _USING_IF_EXISTS;
    } // namespace rel_ops
} // namespace std

export namespace std
{
    using std::gslice _USING_IF_EXISTS;
    using std::gslice_array _USING_IF_EXISTS;
    using std::indirect_array _USING_IF_EXISTS;
    using std::mask_array _USING_IF_EXISTS;
    using std::slice _USING_IF_EXISTS;
    using std::slice_array _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::valarray _USING_IF_EXISTS;
    using std::operator* _USING_IF_EXISTS;
    using std::operator/ _USING_IF_EXISTS;
    using std::operator% _USING_IF_EXISTS;
    using std::operator+ _USING_IF_EXISTS;
    using std::operator- _USING_IF_EXISTS;
    // using std::operator^ _USING_IF_EXISTS;
    // using std::operator& _USING_IF_EXISTS;
    // using std::operator| _USING_IF_EXISTS;
    using std::operator<< _USING_IF_EXISTS;
    using std::operator>> _USING_IF_EXISTS;
    using std::operator&& _USING_IF_EXISTS;
    using std::operator|| _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    // using std::abs _USING_IF_EXISTS;
    // using std::acos _USING_IF_EXISTS;
    // using std::asin _USING_IF_EXISTS;
    // using std::atan _USING_IF_EXISTS;
    // using std::atan2 _USING_IF_EXISTS;
    using std::begin _USING_IF_EXISTS;
    // using std::cos _USING_IF_EXISTS;
    // using std::cosh _USING_IF_EXISTS;
    using std::end _USING_IF_EXISTS;
    // using std::exp _USING_IF_EXISTS;
    // using std::log _USING_IF_EXISTS;
    // using std::log10 _USING_IF_EXISTS;
    // using std::pow _USING_IF_EXISTS;
    // using std::sin _USING_IF_EXISTS;
    // using std::sinh _USING_IF_EXISTS;
    // using std::sqrt _USING_IF_EXISTS;
    // using std::tan _USING_IF_EXISTS;
    // using std::tanh _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    using std::get _USING_IF_EXISTS;
    using std::get_if _USING_IF_EXISTS;
    using std::holds_alternative _USING_IF_EXISTS;
    using std::variant _USING_IF_EXISTS;
    using std::variant_alternative _USING_IF_EXISTS;
    using std::variant_alternative_t _USING_IF_EXISTS;
    using std::variant_npos _USING_IF_EXISTS;
    using std::variant_size _USING_IF_EXISTS;
    using std::variant_size_v _USING_IF_EXISTS;
    using std::operator== _USING_IF_EXISTS;
    using std::operator!= _USING_IF_EXISTS;
    using std::operator< _USING_IF_EXISTS;
    using std::operator> _USING_IF_EXISTS;
    using std::operator<= _USING_IF_EXISTS;
    using std::operator>= _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::bad_variant_access _USING_IF_EXISTS;
    using std::hash _USING_IF_EXISTS;
    using std::monostate _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    using std::visit _USING_IF_EXISTS;
} // namespace std

export namespace std
{
    namespace __format
    {
        using std::vector _USING_IF_EXISTS;
    }
    using std::vector _USING_IF_EXISTS; // bug: ICE
    using std::operator== _USING_IF_EXISTS;
    using std::operator<=> _USING_IF_EXISTS;
    using std::erase _USING_IF_EXISTS;
    using std::erase_if _USING_IF_EXISTS;
    using std::swap _USING_IF_EXISTS;
    namespace pmr
    {
        using std::pmr::vector _USING_IF_EXISTS;
    }
    using std::hash _USING_IF_EXISTS;
} // namespace std