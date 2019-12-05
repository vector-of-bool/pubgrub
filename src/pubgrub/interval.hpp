#pragma once

#include <pubgrub/concepts.hpp>

#include <algorithm>
#include <cassert>
#include <memory>
#include <ostream>
#include <vector>

namespace pubgrub {

template <detail::simple_comparable ElementType, typename Allocator = std::allocator<ElementType>>
class interval_set {
public:
    using element_type   = ElementType;
    using allocator_type = Allocator;

    struct interval_type {
        const element_type& low;
        const element_type& high;

        friend std::ostream& operator<<(std::ostream& out, interval_type iv) noexcept {
            out << "[" << iv.low << ", " << iv.high << ")";
            return out;
        }
    };

private:
    using vec_type = std::vector<element_type, allocator_type>;
    vec_type _points;
    using point_iter = typename vec_type::const_iterator;

    struct pair_iterator {
        point_iter _it;

        using iterator_category = std::random_access_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = interval_type;
        using reference         = const value_type&;
        using pointer           = const value_type*;

        interval_type operator*() const noexcept { return interval_type{*_it, *std::next(_it)}; }

        pair_iterator& operator++() noexcept {
            std::advance(_it, 2);
            return *this;
        }

        friend difference_type operator-(pair_iterator lhs, pair_iterator rhs) noexcept {
            return (lhs._it - rhs._it) / 2;
        }

        bool operator!=(pair_iterator other) const noexcept { return _it != other._it; }

        bool operator==(pair_iterator other) const noexcept { return _it == other._it; }
    };

    class intervals_view {
    public:
        const vec_type& _v;

        pair_iterator begin() const noexcept { return pair_iterator{_v.cbegin()}; }

        pair_iterator end() const noexcept { return pair_iterator{_v.cend()}; }
    };

    auto _find_point_after(const element_type& other_point) const noexcept {
        return std::partition_point(_points.cbegin(),
                                    _points.cend(),
                                    [&](const element_type& my_point) {
                                        return !(other_point < my_point);
                                    });
    }

    auto _find_point_after_or_at(const element_type& other_point) const noexcept {
        return std::partition_point(_points.cbegin(),
                                    _points.cend(),
                                    [&](const element_type& my_point) {
                                        return my_point < other_point;
                                    });
    }

    std::size_t _n_points_before(const element_type& other_point) const noexcept {
        return std::distance(_points.cbegin(), _find_point_after(other_point));
    }

    std::size_t _points_before_or_at(const element_type& other_point) const noexcept {
        return std::distance(_points.cbegin(), _find_point_after_or_at(other_point));
    }

    bool _check(const interval_type& iv, std::size_t parity) const noexcept {
        auto n_points_before = _n_points_before(iv.low);
        return (n_points_before % 2 == parity) && n_points_before == _points_before_or_at(iv.high);
    }

    static void _intersect_one(pair_iterator& left, pair_iterator& right, vec_type& acc) noexcept {
        const auto& [l_low, l_high] = *left;
        const auto& [r_low, r_high] = *right;

        if (r_low < l_low) {
            return _intersect_one(right, left, acc);
        }

        // Known: l_low <= r_low
        if (!(r_low < l_high)) {
            // l: --%%%%%%%%--------
            // r: ----------%%%%%%--
            ++left;  // Discard
            return;
        }

        // Known: l_high > r_low
        if (!(l_high < r_high)) {
            // l: ---%%%%%%%%%%%----
            // r: -------%%%%%------
            // or:
            // l: ---%%%%%%%%%%%----
            // r: -------%%%%%%%----
            acc.push_back(r_low);
            acc.push_back(r_high);
            ++right;
            return;
        }

        assert(l_high < r_high);
        // l: --%%%%%%%%-----
        // r: -----%%%%%%%%--
        acc.push_back(r_low);
        acc.push_back(l_high);
        ++left;
        return;
    }

    void _union_insert(const interval_type& iv) {
        const auto left          = _find_point_after_or_at(iv.low);
        const bool starts_within = (left - _points.begin()) % 2 == 1;
        const auto right         = _find_point_after(iv.high);
        const auto ends_within   = (right - _points.begin()) % 2 == 1;
        if (starts_within && ends_within) {
            _points.erase(left, right);
        } else if (starts_within && !ends_within) {
            const auto nth       = (left - _points.begin());
            const auto new_right = _points.insert(right, iv.high);
            _points.erase(_points.begin() + nth, new_right);
        } else if (ends_within && !starts_within) {
            const auto nth      = (right - _points.begin());
            const auto new_left = _points.insert(left, iv.low);
            _points.erase(std::next(new_left), _points.begin() + nth + 1);
        } else {
            const auto low_nth  = (left - _points.begin());
            const auto high_nth = (right - _points.begin());
            _points.insert(_points.begin() + low_nth, iv.low);
            _points.insert(_points.begin() + high_nth + 1, iv.high);
            _points.erase(_points.begin() + low_nth + 1, _points.begin() + high_nth + 1);
        }
    }

    void _diff_subtract(const interval_type& iv) {
        const auto left          = _find_point_after_or_at(iv.low);
        const bool starts_within = (left - _points.begin()) % 2 == 1;
        const auto right         = _find_point_after(iv.high);
        const bool ends_within   = (right - _points.begin()) % 2 == 1;
        auto       it            = _points.erase(left, right);
        if (starts_within) {
            it = _points.insert(it, iv.low);
            ++it;
        }
        if (ends_within) {
            _points.insert(it, iv.high);
        }
    }

    explicit interval_set(vec_type&& vec)
        : _points(std::move(vec)) {}

public:
    interval_set() = default;
    explicit interval_set(allocator_type alloc)
        : _points(alloc) {}

    interval_set(element_type left, element_type right)
        : interval_set(left, right, allocator_type()) {}

    interval_set(element_type left, element_type right, allocator_type alloc)
        : _points({left, right}, alloc) {
        assert(left < right && "Invalid initial interval");
    }

    intervals_view iter_intervals() const noexcept { return intervals_view{_points}; }

    bool contains(const element_type& point) const noexcept {
        return _n_points_before(point) % 2 == 1;
    }

    bool contains(const interval_type& iv) const noexcept { return _check(iv, 1); }

    bool disjoint(const interval_type& iv) const noexcept { return _check(iv, 0); }

    bool contains(const interval_set& other) const noexcept {
        auto other_it = other.iter_intervals();
        return std::all_of(other_it.begin(), other_it.end(), [&](const interval_type& iv) {
            return contains(iv);
        });
    }

    bool contained_by(const interval_set& other) const noexcept { return other.contains(*this); }

    bool disjoint(const interval_set& other) const noexcept {
        auto other_it = other.iter_intervals();
        return std::all_of(other_it.begin(), other_it.end(), [&](const interval_type& iv) {
            return disjoint(iv);
        });
    }

    std::size_t num_intervals() const noexcept { return _points.size() / 2; }
    bool        empty() const noexcept { return num_intervals() == 0; }

    interval_set union_(const interval_set& other) const noexcept {
        auto ret = *this;
        for (const interval_type& iv : other.iter_intervals()) {
            ret._union_insert(iv);
        }
        assert(std::is_sorted(ret._points.begin(), ret._points.end()));
        return ret;
    }

    interval_set difference(const interval_set& other) const noexcept {
        auto ret = *this;
        for (const interval_type& iv : other.iter_intervals()) {
            ret._diff_subtract(iv);
        }
        assert(std::is_sorted(ret._points.begin(), ret._points.end()));
        return ret;
    }

    interval_set intersection(const interval_set& other) const noexcept {
        const auto my_iter    = iter_intervals();
        const auto other_iter = other.iter_intervals();
        auto       my_it      = my_iter.begin();
        const auto my_end     = my_iter.end();
        auto       other_it   = other_iter.begin();
        const auto other_end  = other_iter.end();
        vec_type   acc{_points.get_allocator()};
        while (my_it != my_end && other_it != other_end) {
            _intersect_one(my_it, other_it, acc);
        }
        return interval_set(std::move(acc));
    }

    friend bool operator==(const interval_set& lhs, const interval_set& rhs) noexcept {
        return std::equal(lhs._points.cbegin(),
                          lhs._points.cend(),
                          rhs._points.cbegin(),
                          rhs._points.cend(),
                          [](const element_type& lhs, const element_type& rhs) {
                              return !(lhs < rhs) && !(rhs < lhs);
                          });
    }

    friend std::ostream& operator<<(std::ostream& out, const interval_set& rhs) noexcept {
        auto       ivs = rhs.iter_intervals();
        auto       it  = ivs.begin();
        const auto end = ivs.end();
        while (it != end) {
            out << *it;
            ++it;
            if (it != end) {
                out << " or ";
            }
        }
        return out;
    }
};

}  // namespace pubgrub