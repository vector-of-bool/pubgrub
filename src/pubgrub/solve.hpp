#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/debug.hpp>
#include <pubgrub/failure.hpp>
#include <pubgrub/incompatibility.hpp>
#include <pubgrub/partial_solution.hpp>
#include <pubgrub/term.hpp>

#include <neo/tl.hpp>

#include <initializer_list>
#include <iostream>
#include <list>
#include <set>
#include <variant>
#include <vector>

namespace pubgrub {

namespace detail {

namespace sr = std::ranges;

template <typename IC>
class ic_record;

template <typename Requirement, typename Allocator>
class ic_record<pubgrub::incompatibility<Requirement, Allocator>> {
    using requirement_type    = Requirement;
    using allocator_type      = Allocator;
    using ic_type             = incompatibility<requirement_type, allocator_type>;
    using conflict_cause_type = typename ic_type::conflict_cause;

    using term_type = typename ic_type::term_type;
    using key_type  = typename term_type::key_type;

    template <typename T>
    using rebind_alloc_t = detail::rebind_alloc_t<allocator_type, T>;

    using ic_ref_vec = std::vector<std::reference_wrapper<const ic_type>,
                                   rebind_alloc_t<std::reference_wrapper<const ic_type>>>;

    struct ic_by_key_seq {
        key_type   key;
        ic_ref_vec ics;
    };

    allocator_type _alloc;

    // Use std::list so elements to not move after creations
    using list_type = std::list<ic_type, rebind_alloc_t<ic_type>>;
    list_type _ics{_alloc};

    using ic_by_key_seq_vec = std::vector<ic_by_key_seq, rebind_alloc_t<ic_by_key_seq>>;
    ic_by_key_seq_vec _by_key{_alloc};

    auto _seq_for_key(const key_type& key_) const noexcept {
        return sr::partition_point(_by_key, [&](auto&& el) { return el.key < key_; });
    }

    auto _seq_for_key(const key_type& key_) noexcept {
        return sr::partition_point(_by_key, [&](auto&& el) { return el.key < key_; });
    }

    const ic_type& _add_ic_to_err(std::list<ic_type>& ics, const ic_type& ic) noexcept {
        auto cause    = ic.cause();
        auto conflict = std::get_if<conflict_cause_type>(&cause);
        if (conflict) {
            const ic_type& left  = _add_ic_to_err(ics, conflict->left);
            const ic_type& right = _add_ic_to_err(ics, conflict->right);
            return ics.emplace_back(ic.terms(), _alloc, conflict_cause_type{left, right});
        } else {
            return ics.emplace_back(ic.terms(), _alloc, ic.cause());
        }
    }

    unsolvable_failure<ic_type> _build_exception(const ic_type& root) noexcept {
        std::list<ic_type> ics;
        _add_ic_to_err(ics, root);
        return unsolvable_failure<ic_type>(std::move(ics));
    }

public:
    explicit ic_record(allocator_type ac)
        : _alloc(ac) {}

    template <typename... Args>
    ic_type& emplace_record(Args&&... args) noexcept {
        auto& new_ic = _ics.emplace_back(std::forward<Args>(args)...);

        for (const term_type& term : new_ic.terms()) {
            auto existing = _seq_for_key(term.key());
            if (existing == _by_key.end() || existing->key != term.key()) {
                existing = _by_key.insert(existing, {term.key(), ic_ref_vec{_alloc}});
            }
            existing->ics.push_back(std::cref(new_ic));
        }
        return new_ic;
    }

    const auto& all() const noexcept { return _ics; }

    const auto& for_name(const key_type& k) const noexcept {
        auto seq_iter = _seq_for_key(k);
        assert(seq_iter != _by_key.cend());
        return seq_iter->ics;
    }

    [[noreturn]] void throw_failure(const ic_type& root) { throw _build_exception(root); }
};

template <requirement Req, provider<Req> P, typename Allocator = std::allocator<Req>>
struct solver {
    using requirement_type    = Req;
    using provider_type       = P;
    using allocator_type      = Allocator;
    using ic_type             = incompatibility<requirement_type, allocator_type>;
    using sln_type            = partial_solution<requirement_type, allocator_type>;
    using term_type           = typename ic_type::term_type;
    using key_type            = typename term_type::key_type;
    using conflict_cause_type = typename ic_type::conflict_cause;

    template <typename T>
    using rebind_alloc = detail::rebind_alloc_t<allocator_type, T>;

    using key_set_type = std::set<key_type, std::less<>, rebind_alloc<key_type>>;
    struct conflict {};
    struct no_conflict {};
    struct almost_conflict {
        const term_type& term;
    };

    using conflict_result = std::variant<conflict, no_conflict, almost_conflict>;

    provider_type& provider;

    allocator_type alloc{};

    ic_record<ic_type> ics{alloc};
    key_set_type       changed = key_set_type(rebind_alloc<key_type>(alloc));
    sln_type           sln{alloc};

    void _debug(std::string_view sv, const auto&... args) const {
        debug::debug(provider, sv, args...);
    }

    void preload_root(requirement_type req) noexcept {
        _debug("Loading root dependency: {}", neo::repr(req));
        auto& t = ics.emplace_record(std::vector{term_type{req, false}},
                                     alloc,
                                     typename ic_type::root_cause{});
        _debug("Incompatibility created from root requirement: {}", neo::repr_value(t));
        changed.insert(key_of(req));
    }

    auto solve() {
        for (; !changed.empty(); speculate_one_decision()) {
            unit_propagation();
        }

        _debug("Solution complete! {}", neo::repr_value(sln));
        return sln.completed_solution();
    }

    void speculate_one_decision() {
        const requirement_type* next_req = sln.next_unsatisfied_term();
        if (!next_req) {
            return;
        }

        _debug("Speculating next unsatisfied term: {}", debug::try_repr{*next_req});

        // Find the best candidate package for the term
        const auto& cand_req = provider.best_candidate(*next_req);
        if (!cand_req) {
            _debug("Provider failed to find a best candidate for the requirement");
            ics.emplace_record(std::vector{term_type{*next_req, true}},
                               alloc,
                               typename ic_type::unavailable_cause{});
            changed.insert(key_of(*next_req));
            return;
        }

        _debug("Best candidate of {} is {}. Looking up requirements.",
               debug::try_repr{*next_req},
               debug::try_repr{*cand_req});

        auto&& cand_reqs      = provider.requirements_of(*cand_req);
        bool   found_conflict = false;
        for (requirement_type req : cand_reqs) {
            _debug("Requirement of {}: {}", debug::try_repr{*cand_req}, debug::try_repr{req});
            if (key_of(req) == key_of(*cand_req)) {
                throw std::runtime_error("Package cannot depend on itself.");
            }
            const ic_type& new_ic
                = ics.emplace_record(std::vector{term_type{*cand_req},
                                                 term_type{std::move(req), false}},
                                     alloc,
                                     typename ic_type::dependency_cause{});
            _debug("  Incompatibility derived from dependency: {}", neo::repr_value(new_ic));
            assert(new_ic.terms().size() == 2);
            bool this_conflicts = std::all_of(new_ic.terms().cbegin(),
                                              new_ic.terms().cend(),
                                              [&](const term_type& ic_term) {
                                                  return ic_term.key() == key_of(*cand_req)
                                                      || sln.satisfies(ic_term);
                                              });
            if (this_conflicts) {
                _debug("  CONFLICT: This incompatibility is satisfied by the partial solution: {}",
                       neo::repr_value(sln));
            }
            found_conflict = found_conflict or this_conflicts;
        }

        if (!found_conflict) {
            _debug(
                "No conflict was found. Recording speculation as a decision for the partial "
                "solution");
            sln.record_decision(term_type{*cand_req});
            _debug("New partial solution: {}", neo::repr_value(sln));
        }

        changed.insert(key_of(*cand_req));
    }

    /**
     * @brief Perform unit propagation until there are not pending changes
     */
    void unit_propagation() {
        while (!changed.empty()) {
            auto next_unit = changed.extract(changed.begin());
            propagate_for(next_unit.value());
        }
    }

    /**
     * @brief Perform unit propagation for the (pkg of) the given key
     */
    void propagate_for(const key_type& k) {
        neo_assertion_breadcrumbs("Performing unit propagation", k);
        _debug("Performing unit propagation for {}", debug::try_repr{k});
        auto ics_for_name = ics.for_name(k);
        for (const ic_type& ic : ics_for_name) {
            if (!propagate_one(ic)) {
                break;
            }
        }
    }

    /**
     * @brief Propagate a single given incompatibility
     *
     * @return true If we have more work to do
     * @return false When we are done with this incompatibility
     */
    bool propagate_one(const ic_type& ic) {
        neo_assertion_breadcrumbs("Propagating incompatibility", ic);
        _debug("  Propagating incompatibility: {}", neo::repr_value(ic));
        auto res = check_conflict(ic);

        if (auto almost = std::get_if<almost_conflict>(&res)) {
            auto inv = almost->term.inverse();
            _debug("  Deriving {} as the inverse of the sole unsatisfied term, derived from {}",
                   neo::repr_value(inv),
                   neo::repr_value(ic));
            sln.record_derivation(std::move(inv), ic);
            _debug("  New partial solution state: {}", neo::repr_value(sln));
            changed.insert(almost->term.key());
        } else if (std::holds_alternative<conflict>(res)) {
            // The cause of the conflict
            _debug("  Performing conflict resolution for {}", neo::repr_value(ic));
            const ic_type& root_cause = resolve_conflict(ic);
            _debug("  Determined root cause of conflict to be {}", neo::repr_value(root_cause));
            auto res2    = check_conflict(root_cause);
            auto almost2 = std::get_if<almost_conflict>(&res2);
            neo_assert(invariant,
                       std::holds_alternative<almost_conflict>(res2),
                       "Expected conflict resolution term to be an almost-conflict with the "
                       "partial solution so that we can make a subsequence derivation from it.",
                       ic,
                       root_cause);
            auto inv = almost2->term.inverse();
            _debug(
                "  Deriving {} as the inverse of the sole unsatisfied term of the conflict-causing "
                "incompatibility {}",
                neo::repr_value(inv),
                neo::repr_value(root_cause));
            sln.record_derivation(std::move(inv), root_cause);
            _debug("  New partial solution state: ", neo::repr_value(sln));
            changed.clear();
            changed.insert(almost2->term.key());
            return false;
        } else {
            assert(std::holds_alternative<no_conflict>(res));
            // Nothing to do
        }
        return true;
    }

    conflict_result check_conflict(const ic_type& ic) const noexcept {
        neo_assertion_breadcrumbs("Checking for conflicts", ic);
        _debug("  Checking for conflicts with term {}", neo::repr_value(ic));

        const term_type* unsat_term = nullptr;
        for (const term_type& term : ic.terms()) {
            _debug("    Check term: {}", neo::repr_value(term));
            auto rel = sln.relation_to(term);
            if (rel == set_relation::disjoint) {
                _debug("      Disjoint: No conflict is possible.");
                return no_conflict{};
            } else if (rel == set_relation::overlap) {
                _debug("      Overlap");
                if (unsat_term != nullptr) {
                    _debug(
                        "      More than one term overlaps: No information can be derived from "
                        "this incompatibility.");
                    return no_conflict{};
                }
                unsat_term = &term;
            } else {
                // Term is satisfied
                _debug("      Satisfied");
            }
        }

        if (unsat_term == nullptr) {
            _debug("    All terms are satisfied. Incompatibility {} creates a conflict.",
                   neo::repr_value(ic));
            return conflict{};
        }

        _debug("    One term ({}) is unsatisfied.", neo::repr_value(*unsat_term));
        return almost_conflict{*unsat_term};
    }

    const ic_type& resolve_conflict(std::reference_wrapper<const ic_type> ic_) {
        const ic_type& original_ic = ic_;
        _debug("  Backtracking from conflicting incompatibility: {}", neo::repr_value(original_ic));
        while (true) {
            const ic_type& ic = ic_;
            neo_assertion_breadcrumbs("Performing conflict resolution", original_ic, ic);
            const auto& opt_bt_info = sln.build_backtrack_info(ic.terms());
            if (!opt_bt_info) {
                // There is nowhere left to backtrack to: There is no possible
                // solution!
                _debug(
                    "  No backtracking target! We've hit a root incompatibility. Dependency "
                    "resolution fails.");
                ics.throw_failure(ic);
            }
            const auto& [term, satisfier, prev_sat_level, difference] = *opt_bt_info;
            if (satisfier.is_decision() || prev_sat_level < satisfier.decision_level) {
                _debug("  Found backtrack target on assignment: {}", neo::repr_value(satisfier));
                sln.backtrack_to(prev_sat_level);
                return ic;
            }
            _debug("    Stepping back through assignment: {}", neo::repr_value(satisfier));
            assert(satisfier.cause);
            typename ic_type::term_vec new_terms{alloc};
            for (const auto& t : ic.terms()) {
                if (&t != &term) {
                    new_terms.push_back(t);
                }
            }
            for (const auto& t : satisfier.cause->terms()) {
                if (t.key() != satisfier.term.key()) {
                    new_terms.push_back(t);
                }
            }
            if (difference) {
                _debug("    Inferred difference term: {}", neo::repr_value(*difference));
                new_terms.push_back(difference->inverse());
            }
            neo_assert(expects,
                       sr::all_of(new_terms, [&](auto&& term) { return sln.satisfies(term); }),
                       "Expected conflict resolution term to be satisfied by partial solution",
                       new_terms);

            ic_ = ics.emplace_record(std::move(new_terms),
                                     alloc,
                                     conflict_cause_type{ic, *satisfier.cause});
            _debug("  Derived new intermediate incompatibility: ({})", neo::repr_value(ic_.get()));
            neo_assert(expects,
                       std::holds_alternative<conflict>(check_conflict(ic_)),
                       "Expected new derived incompatibility to be in conflict with the "
                       "partial solution");
        }
    }
};

}  // namespace detail

template <requirement_range Range, provider<std::ranges::range_value_t<Range>> P>
decltype(auto) solve(Range&& c, P&& p) {
    neo_assertion_breadcrumbs("Solving dependency set", debug::try_repr{c}, debug::try_repr{p});
    debug::debug(p, "Solving given dependencies: {}", neo::repr(debug::try_repr{c}));
    detail::solver<std::ranges::range_value_t<Range>, P> solver{p};
    for (auto&& req : c) {
        solver.preload_root(req);
    }
    return solver.solve();
}

template <requirement Req, provider<Req> P>
decltype(auto) solve(std::initializer_list<term<Req>> il, P&& p) {
    return solve(std::ranges::subrange{il.begin(), il.end()}, p);
}

}  // namespace pubgrub