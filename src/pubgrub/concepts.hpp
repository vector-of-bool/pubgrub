#pragma once

#include <neo/invoke.hpp>

#include <iterator>
#include <ranges>
#include <type_traits>

namespace pubgrub {

// clang-format offf
namespace detail {

template <typename B>
concept boolean = requires(const B b) {
    {b ? 0 : 0};
};

template <typename T>
concept equality_comparable = requires(const T& value) {
    { value == value } -> boolean;
    { value != value } -> boolean;
};

template <typename From, typename To>
concept decays_to = std::same_as<std::decay_t<From>, To>;

template <typename Iter, typename Type>
concept iterator_of
    = std::input_iterator<Iter> && std::convertible_to<std::iter_value_t<Iter>, Type>;

template <typename R, typename T>
concept range_of = std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template <typename Opt, typename Type>
concept optional_like = boolean<Opt> && requires(const Opt what) {
    { *what } -> std::convertible_to<Type>;
};

template <typename Allocator, typename Type>
using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<Type>;

template <typename T>
constexpr decltype(auto) key_of_impl(const T& value) noexcept {
    return neo::invoke(&T::key, value);
}

}  // namespace detail

template <typename T>
concept key = std::totally_ordered<T> && std::semiregular<T>;

template <typename T>
concept keyed = requires(const T item) {
    { neo::invoke(&std::remove_cvref_t<T>::key, item) }
    noexcept;
    requires key<std::remove_cvref_t<decltype(detail::key_of_impl(item))>>;
};

template <keyed K>
constexpr decltype(auto) key_of(const K& k) noexcept {
    return detail::key_of_impl(k);
}

template <keyed T>
using key_type_t = std::remove_cvref_t<decltype(key_of(std::declval<T&&>()))>;

template <typename T>
concept requirement = keyed<T> && requires(const T req) {
    { req.implied_by(req) } -> detail::boolean;
    { req.excludes(req) } -> detail::boolean;
    { req.intersection(req) } -> detail::optional_like<T>;
    { req.union_(req) } -> detail::optional_like<T>;
    { req.difference(req) } -> detail::optional_like<T>;
};

template <typename Iter>
concept requirement_iterator = std::input_iterator<Iter> && requirement<std::iter_value_t<Iter>>;

template <typename R>
concept requirement_range
    = std::ranges::input_range<R> && requirement<std::ranges::range_value_t<R>>;

// clang-format on

}  // namespace pubgrub