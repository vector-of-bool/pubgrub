#pragma once

#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>

namespace pubgrub {

// clang-format off
namespace detail {

template <typename T, typename U>
concept same_as = std::is_same_v<T, U>;

template <typename B>
concept boolean = requires(const B b) {
    { b ? 0 : 0 };
};

template <typename T>
concept simple_comparable = requires(const T value) {
    { value < value } -> boolean;
};

template <typename T>
concept equality_comparable = requires(const T& value) {
    { value == value } -> boolean;
    { value != value } -> boolean;
};

template <typename From, typename To>
concept convertible_to = std::is_convertible_v<From, To>;

template <typename From, typename To>
concept constructible_to = std::is_constructible_v<To, From>;

template <typename From, typename To>
concept decays_to = same_as<std::decay_t<From>, To>;

template <typename Iter>
concept input_iterator = requires(Iter iter, const Iter c_iter) {
    { c_iter != c_iter } -> boolean;
    { *c_iter };
    { ++iter };
};

template <input_iterator Iter>
using value_type_t = typename std::iterator_traits<Iter>::value_type;

template <input_iterator Iter, typename Type>
concept iterator_of = convertible_to<value_type_t<Iter>, Type>;

template <typename R>
concept range = requires(R range) {
    { std::begin(range) } -> input_iterator;
    { std::begin(range) != std::end(range) } -> detail::boolean;
};

template <range R>
using iterator_type_t = std::decay_t<decltype(std::begin(std::declval<R>()))>;

template <range R>
using range_value_type_t = value_type_t<iterator_type_t<R>>;

template <range R, typename T>
concept range_of = same_as<range_value_type_t<R>, T>;

template <typename T>
concept copyable = std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;

template <boolean Opt, typename Type>
concept optional_like = requires(const Opt what) {
    { *what } -> convertible_to<Type>;
};

template <typename Allocator, typename Type>
using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<Type>;

} // namespace detail

template <typename T>
concept key =
    detail::simple_comparable<T> &&
    detail::copyable<T>;

template <typename T>
concept keyed = requires(const T item) {
    { &T::key };
    { std::invoke(&T::key, item) } -> key;
};

template <keyed K>
decltype(auto) key_of(const K& k) {
    return std::invoke(&K::key, k);
}

template <keyed T>
using key_type_t = std::decay_t<decltype(key_of(std::declval<T&&>()))>;

template <typename T>
concept set = requires(const T s) {
    { s.contains(s) } -> detail::boolean;
    { s.disjoint(s) } -> detail::boolean;
    { s.intersection(s) } -> detail::same_as<T>;
    { s.union_(s) } -> detail::same_as<T>;
    { s.difference(s) } -> detail::same_as<T>;
};

template <keyed T>
concept requirement = requires(const T req) {
    { req.implied_by(req) } -> detail::boolean;
    { req.excludes(req) } -> detail::boolean;
    { req.intersection(req) } -> detail::optional_like<T>;
    { req.union_(req) } -> detail::optional_like<T>;
    { req.difference(req) } -> detail::optional_like<T>;
};

template <key K>
bool keys_equivalent(const K& left, const K& right) noexcept {
    if constexpr (detail::equality_comparable<K>) {
        return left == right;
    } else {
        return !(left < right || right < left);
    }
}

template <detail::input_iterator Iter>
concept requirement_iterator = requirement<detail::value_type_t<Iter>>;

template <detail::range R>
concept requirement_range = requirement_iterator<detail::iterator_type_t<R>>;

// clang-format on

}  // namespace pubgrub