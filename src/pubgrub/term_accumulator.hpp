#pragma once

#include <pubgrub/term.hpp>

#include <map>

namespace pubgrub {

template <requirement Req, typename Allocator>
class term_accumulator {
public:
    using requirement_type = Req;
    using allocator_type   = Allocator;
    using term_type        = term<requirement_type>;
    using key_type         = typename term_type::key_type;
    using term_map
        = std::map<key_type,
                   term_type,
                   std::less<>,
                   detail::rebind_alloc_t<allocator_type, std::pair<const key_type, term_type>>>;

private:
    term_map _positives;
    term_map _negatives;

    void _add_to(const term_type& t, term_map& map) {
        // Try to insert this new term into the map
        const auto [existing, did_insert] = map.try_emplace(t.key(), t);
        if (!did_insert) {
            // Entry already exists. Shrink the entry to the intersection formed
            // with our new term
            auto isect = existing->second.intersection(t);
            assert(isect.has_value());
            existing->second = std::move(*isect);
        }
    }

public:
    explicit term_accumulator(allocator_type alloc)
        : _positives(alloc)
        , _negatives(alloc) {}

    void add(const term_type& term) noexcept {
        if (term.positive) {
            _add_to(term, _positives);
            auto neg_it = _negatives.find(term.key());
            if (neg_it != _negatives.end()) {
                _negatives.erase(neg_it);
            }
        } else {
            _add_to(term, _negatives);
        }
    }

    void clear() noexcept {
        _positives.clear();
        _negatives.clear();
    }

    const term_type* term_for(const key_type& key) const noexcept {
        auto pos_it = _positives.find(key);
        if (pos_it != _positives.end()) {
            return &pos_it->second;
        }
        auto neg_it = _negatives.find(key);
        if (neg_it != _negatives.end()) {
            return &neg_it->second;
        }
        return nullptr;
    }
};

}  // namespace pubgrub