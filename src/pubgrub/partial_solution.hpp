#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/incompatibility.hpp>
#include <pubgrub/term.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>

namespace pubgrub {

template <requirement Req, typename Allocator = std::allocator<Req>>
class partial_solution {
public:
    using requirement_type     = Req;
    using allocator_type       = Allocator;
    using term_type            = term<requirement_type>;
    using key_type             = typename term_type::key_type;
    using incompatibility_type = incompatibility<requirement_type, allocator_type>;

    struct assignment {
        term_type                   term;
        std::size_t                 decision_level;
        const incompatibility_type* cause;
        bool                        is_decision() const noexcept { return cause == nullptr; }

        friend void do_repr(auto out, const assignment* self) noexcept {
            constexpr bool can_repr_req = decltype(out)::template can_repr<requirement_type>;
            if constexpr (can_repr_req) {
                out.type("pubgrub::partial_solution::assignment<{}>",
                         out.template repr_type<requirement_type>());
            } else {
                out.type("pubgrub::partial_solution::assignment<[…]>");
            }
            if (self) {
                out.bracket_value("term={}, decision_level={}, cause={}",
                                  out.repr_value(self->term),
                                  out.repr_value(self->decision_level),
                                  out.repr_value(self->cause));
            }
        }
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

    void _register(const term_type& t) {
        neo_assertion_breadcrumbs("Narrowing assignment caches", t);
        const auto pos_it = _positives.find(t.key());
        if (pos_it != _positives.end()) {
            auto opt_is = pos_it->second.intersection(t);
            neo_assert(expects,
                       opt_is.has_value(),
                       "Intersection resulted in a null term, but we expected to narrow down an "
                       "existing term that was overlapping",
                       pos_it->second,
                       t);
            pos_it->second = std::move(*opt_is);
            return;
        }

        auto term   = t;
        auto neg_it = _negatives.find(term.key());
        if (neg_it != _negatives.end()) {
            auto opt_t = t.intersection(neg_it->second);
            neo_assert(expects,
                       opt_t.has_value(),
                       "Intersection resulted in a null term, but we expected to narrow down an "
                       "existing term that was overlapping",
                       neg_it->second,
                       t);
            term = std::move(*opt_t);
        }

        if (term.positive) {
            if (neg_it != _negatives.end()) {
                // Remove the assignment from the negatives list
                _negatives.erase(neg_it);
            }
            neo_assert(invariant,
                       _positives.find(t.key()) == _positives.end(),
                       "Positive term was not inserted as the final element in the positives list",
                       term);
            _positives.emplace(t.key(), std::move(term));
        } else {
            _negatives.insert_or_assign(t.key(), std::move(term));
        }
    }

    static set_relation _relation_to(const term_type& term,
                                     const term_map&  positives,
                                     const term_map&  negatives) noexcept {
        auto pos_it = positives.find(term.key());
        if (pos_it != positives.end()) {
            return pos_it->second.relation_to(term);
        }

        auto neg_it = negatives.find(term.key());
        if (neg_it != negatives.end()) {
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
            if (as.is_decision()) {
                ret.push_back(as.term.requirement);
            }
        }
        return ret;
    }

    void record_derivation(term_type term, const incompatibility_type& cause) noexcept {
        neo_assertion_breadcrumbs("Recording new derivation", term, cause);
        auto& inserted
            = _assignments.emplace_back(assignment{std::move(term), _decided_keys.size(), &cause});
        _register(inserted.term);
    }

    void record_decision(term_type term) noexcept {
        neo_assertion_breadcrumbs("Recording new decision", term);
        [[maybe_unused]] const auto did_insert = _decided_keys.emplace(term.key()).second;
        assert(did_insert && "More than one decision recorded for a single item");

        auto& inserted
            = _assignments.emplace_back(assignment{std::move(term), _decided_keys.size(), nullptr});
        assert(inserted.term.positive);
        _register(inserted.term);
    }

    bool satisfies(const term_type& term) const noexcept {
        return relation_to(term) == set_relation::subset;
    }

    set_relation relation_to(const term_type& term) const noexcept {
        return _relation_to(term, _positives, _negatives);
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

    void backtrack_to(std::size_t decision_level) noexcept {
        neo_assertion_breadcrumbs("Backtracking partial solution",
                                  _assignments.back().decision_level,
                                  decision_level);
        while (_assignments.back().decision_level > decision_level) {
            _assignments.pop_back();
        }
        _positives.clear();
        _negatives.clear();
        _decided_keys.clear();
        for (const auto& as : _assignments) {
            _register(as.term);
            if (as.is_decision()) {
                _decided_keys.insert(as.term.key());
            }
        }
    }

    const assignment& satisfier_of(const term_type& term) const noexcept {
        std::optional<term_type> assigned_term;

        for (const assignment& as : _assignments) {
            if (as.term.key() != term.key()) {
                continue;
            }

            if (!assigned_term) {
                assigned_term = as.term;
            } else {
                assigned_term = assigned_term->intersection(as.term);
            }

            if (assigned_term->implies(term)) {
                return as;
            }
        }
        neo_assert(expects,
                   false,
                   "Unreachable: Attempted to look up the satisfier of a term that was expected to "
                   "be satisfied by the partial solution, but no partial solution assignment was "
                   "found that satisfied the term.",
                   term,
                   assigned_term,
                   _assignments);
        std::terminate();
    }

    struct backtrack_info {
        const term_type&         term;
        const assignment&        satisfier;
        const std::size_t        prev_sat_level;
        std::optional<term_type> difference;
    };

    template <detail::range_of<term_type> Terms>
    std::optional<backtrack_info> build_backtrack_info(const Terms& ts) const {
        const term_type*         most_recent_term      = nullptr;
        const assignment*        most_recent_satisfier = nullptr;
        std::optional<term_type> difference;

        std::size_t previous_satisfier_level = 0;

        for (const term_type& term : ts) {
            const assignment& satisfier = satisfier_of(term);
            if (most_recent_satisfier == nullptr) {
                most_recent_term      = &term;
                most_recent_satisfier = &satisfier;
            } else if (most_recent_satisfier < &satisfier) {
                previous_satisfier_level
                    = (std::max)(previous_satisfier_level, most_recent_satisfier->decision_level);
                most_recent_term      = &term;
                most_recent_satisfier = &satisfier;
                difference            = std::nullopt;
            } else {
                previous_satisfier_level
                    = (std::max)(previous_satisfier_level, satisfier.decision_level);
            }
            if (most_recent_term == &term) {
                difference = most_recent_satisfier->term.difference(*most_recent_term);
                if (difference) {
                    previous_satisfier_level
                        = (std::max)(satisfier_of(difference->inverse()).decision_level,
                                     previous_satisfier_level);
                }
            }
        }

        if (most_recent_satisfier) {
            struct info {};
            assert(most_recent_term);
            assert(most_recent_satisfier);
            return backtrack_info{*most_recent_term,
                                  *most_recent_satisfier,
                                  previous_satisfier_level,
                                  std::move(difference)};
        } else {
            return std::nullopt;
        }
    }
};

}  // namespace pubgrub