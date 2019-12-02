#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/incompatibility.hpp>
#include <pubgrub/term.hpp>
#include <pubgrub/term_accumulator.hpp>

#include <iostream>
#include <map>
#include <set>
#include <vector>

#ifndef PUBGRUB_DEBUG
#define PUBGRUB_DEBUG 0
#endif

#if PUBGRUB_DEBUG != 0 && PUBGRUB_DEBUG != 1
#error "PUBGRUB_DEBUG should be defined to either `0` or `1`"
#endif

namespace pubgrub {

namespace detail {

template <typename... Ts>
void debug_say(const Ts&... args) {
    if constexpr (PUBGRUB_DEBUG) {
        ((std::cerr << args), ...);
        std::cerr << "\n";
    }
}

}  // namespace detail

template <requirement Req, typename Allocator = std::allocator<Req>>
class partial_solution {
public:
    using requirement_type     = Req;
    using allocator_type       = Allocator;
    using term_type            = term<requirement_type>;
    using key_type             = typename term_type::key_type;
    using incompatibility_type = incompatibility<requirement_type, allocator_type>;

    enum class assignment_kind {
        decision,
        derivation,
    };
    struct assignment {
        term_type                   term;
        assignment_kind             kind;
        std::size_t                 decision_level;
        const incompatibility_type* cause;
    };

private:
    using assignment_allocator_type = detail::rebind_alloc_t<allocator_type, assignment>;
    using assignment_vec            = std::vector<assignment, assignment_allocator_type>;
    using term_map_alloc_type
        = detail::rebind_alloc_t<allocator_type, std::pair<const key_type, term_type>>;
    using term_map           = std::map<key_type, term_type, std::less<>, term_map_alloc_type>;
    using key_allocator_type = detail::rebind_alloc_t<allocator_type, key_type>;
    using key_set            = std::set<key_type, std::less<>, key_allocator_type>;

    allocator_type _alloc;
    assignment_vec _assignments{assignment_allocator_type(_alloc)};
    term_map       _positives{term_map_alloc_type(_alloc)};
    term_map       _negatives{term_map_alloc_type(_alloc)};
    key_set        _decided_keys{key_allocator_type(_alloc)};

    static void _record_or_restrict(term_map& map, const term_type& t) {
        // Try to insert this new term into the map
        auto key                          = t.key();
        const auto [existing, did_insert] = map.try_emplace(std::move(key), t);
        if (!did_insert) {
            // Entry already exists. Shrink the entry to the intersection formed
            // with our new term
            auto isect = existing->second.intersection(t);
            assert(isect.has_value());
            existing->second = std::move(*isect);
        }
    }

    static void _record_term(const term_type& t, term_map& positives, term_map& negatives) {
        if (t.positive) {
            _record_or_restrict(positives, t);
            // If we recorded a negative state for this term, drop it
            auto negative = negatives.find(t.key());
            if (negative != negatives.end()) {
                negatives.erase(negative);
            }
        } else {
            _record_or_restrict(negatives, t);
        }
    }

    static set_relation _relation_to(const term_type& term,
                                     const term_map&  positives,
                                     const term_map&  negatives,
                                     const term_type* additional) noexcept {
        auto pos_it = positives.find(term.key());
        if (pos_it != positives.end()) {
            if (additional && keys_equivalent(additional->key(), pos_it->second.key())) {
                auto isect = pos_it->second.intersection(*additional);
                if (!isect) {
                    return set_relation::disjoint;
                }
                return isect->relation_to(term);
            }
            return pos_it->second.relation_to(term);
        }

        auto neg_it = negatives.find(term.key());
        if (neg_it != negatives.end()) {
            if (additional && keys_equivalent(additional->key(), neg_it->second.key())) {
                auto isect = neg_it->second.intersection(*additional);
                if (!isect) {
                    return set_relation::disjoint;
                }
                return isect->relation_to(term);
            }
            return neg_it->second.relation_to(term);
        }

        return set_relation::overlap;
    }

public:
    partial_solution() = default;
    explicit partial_solution(allocator_type alloc)
        : _alloc(alloc) {}

    std::vector<requirement_type, allocator_type> completed_solution() const noexcept {
        std::vector<requirement_type, allocator_type> ret{_alloc};
        for (const assignment& as : _assignments) {
            if (as.kind == assignment_kind::decision) {
                ret.push_back(as.term.requirement);
            }
        }
        return ret;
    }

    void record_derivation(
        term_type                                                         term,
        std::optional<std::reference_wrapper<const incompatibility_type>> opt_cause) noexcept {
        detail::debug_say("New derivation: ", term);
        auto& inserted
            = _assignments.emplace_back(assignment{std::move(term),
                                                   assignment_kind::derivation,
                                                   _assignments.size(),
                                                   opt_cause ? &opt_cause->get() : nullptr});
        _record_term(inserted.term, _positives, _negatives);
    }

    void record_decision(
        term_type                                                         term,
        std::optional<std::reference_wrapper<const incompatibility_type>> opt_cause) noexcept {
        detail::debug_say("New decision: ", term);
        auto& inserted
            = _assignments.emplace_back(assignment{std::move(term),
                                                   assignment_kind::decision,
                                                   _assignments.size(),
                                                   opt_cause ? &opt_cause->get() : nullptr});
        assert(inserted.term.positive);
        _record_term(inserted.term, _positives, _negatives);
        [[maybe_unused]] const auto did_insert = _decided_keys.emplace(inserted.term.key()).second;
        assert(did_insert && "More than one decision recorded for a single item");
    }

    bool satisfies(const term_type& term) const noexcept {
        return relation_to(term) == set_relation::subset;
    }

    set_relation relation_to(const term_type& term) const noexcept {
        return _relation_to(term, _positives, _negatives, nullptr);
    }

    const requirement_type* next_unsatisfied_term() const noexcept {
        auto unsat = std::find_if(  //
            _positives.cbegin(),
            _positives.cend(),
            [&](const auto& pair) noexcept {
                const term_type& cand     = pair.second;
                auto             dec_iter = _decided_keys.find(cand.key());
                return dec_iter == _decided_keys.cend();
            });
        if (unsat != _positives.cend()) {
            return &unsat->second.requirement;
        } else {
            return nullptr;
        }
    }

    using assignment_iterator = typename assignment_vec::const_iterator;

    template <detail::range_of<term_type> Terms>
    assignment_iterator
    first_satisfier_of(const Terms&                                            ts,
                       std::optional<std::reference_wrapper<const assignment>> with
                       = std::nullopt) const noexcept {
        // Find the first assignment that satisfies all terms
        term_map positives{_alloc};
        term_map negatives{_alloc};
        auto     first_sat = std::find_if(  //
            _assignments.cbegin(),
            _assignments.cend(),
            [&](const assignment& as) noexcept {
                using std::begin;
                using std::end;
                _record_term(as.term, positives, negatives);
                return std::all_of(begin(ts), end(ts), [&](const term_type& term) {
                    if (with) {
                        return _relation_to(term, positives, negatives, &with->get().term)
                            == set_relation::subset;
                    } else {
                        return _relation_to(term, positives, negatives, nullptr)
                            == set_relation::subset;
                    }
                });
            });
        assert(first_sat != _assignments.cbegin());
        assert(first_sat != _assignments.cend() && "Unexpected condition while backtracking.");
        return first_sat;
    }

    void backtrack_to(std::size_t decision_level) noexcept {
        auto first_bad = std::partition_point(  //
            _assignments.cbegin(),
            _assignments.cend(),
            [&](const assignment& as) noexcept { return as.decision_level < decision_level; });
        assert(first_bad != _assignments.cend());
        _assignments.erase(first_bad, _assignments.cend());
        _positives.clear();
        _negatives.clear();
        _decided_keys.clear();
        for (const auto& as : _assignments) {
            _record_term(as.term, _positives, _negatives);
            if (as.kind == assignment_kind::decision) {
                _decided_keys.insert(as.term.key());
            }
        }
    }
};

}  // namespace pubgrub