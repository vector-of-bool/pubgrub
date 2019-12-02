#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/term.hpp>

#include <string>

namespace pubgrub::test {

using version = int;

struct simple_version_range {
    version low;
    version high;

    bool contains(simple_version_range other) const noexcept {
        return other.low >= low && other.high <= high;
    }

    bool overlaps(simple_version_range other) const noexcept {
        return low < other.high && high > other.low;
    }

    std::optional<simple_version_range> intersection(simple_version_range other) const noexcept {
        auto max_low  = std::max(low, other.low);
        auto min_high = std::min(high, other.high);
        if (max_low >= min_high) {
            return std::nullopt;
        }
        return simple_version_range{max_low, min_high};
    }
    std::optional<simple_version_range> union_(simple_version_range other) const noexcept {
        if (!overlaps(other)) {
            return std::nullopt;
        }
        auto min_low  = std::min(low, other.low);
        auto max_high = std::max(high, other.high);
        return simple_version_range{min_low, max_high};
    }

    std::optional<simple_version_range> difference(simple_version_range) const noexcept {
        return std::nullopt;
    }

    friend bool operator==(const simple_version_range& lhs,
                           const simple_version_range& rhs) noexcept {
        return lhs.low == rhs.low && lhs.high == rhs.high;
    }

    friend std::ostream& operator<<(std::ostream& out, const simple_version_range& rhs) {
        if (rhs.low + 1 == rhs.high) {
            out << '=' << rhs.low;
        } else {
            out << "[" << rhs.low << ", " << rhs.high << ")";
        }
        return out;
    }
};

struct simple_req {
    using key_type           = std::string;
    using version_range_type = simple_version_range;
    std::string          key;
    simple_version_range range;

    simple_req with_range(simple_version_range r) const noexcept { return {key, r}; }

    std::optional<simple_req> intersection(simple_req o) const noexcept {
        auto isect = range.intersection(o.range);
        if (!isect) {
            return std::nullopt;
        }
        return with_range(*isect);
    }

    std::optional<simple_req> union_(simple_req o) const noexcept {
        auto un = range.union_(o.range);
        if (!un) {
            return std::nullopt;
        }
        return with_range(*un);
    }

    std::optional<simple_req> difference(simple_req o) const noexcept {
        auto diff = range.difference(o.range);
        if (!diff) {
            return std::nullopt;
        }
        return with_range(*diff);
    }

    auto implied_by(simple_req other) const noexcept { return range.contains(other.range); }
    auto excludes(simple_req other) const noexcept { return !range.overlaps(other.range); }

    friend bool operator==(const simple_req& lhs, const simple_req& rhs) noexcept {
        return std::tie(lhs.key, lhs.range) == std::tie(rhs.key, rhs.range);
    }

    friend std::ostream& operator<<(std::ostream& out, const simple_req& req) {
        out << req.key << ' ' << req.range;
        return out;
    }
};

using simple_term = pubgrub::term<simple_req>;

template <pubgrub::requirement R>
void check_req(R) {}

inline void test_concepts() { check_req(simple_req()); }

}  // namespace pubgrub::test
