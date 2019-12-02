#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/incompatibility.hpp>
#include <pubgrub/partial_solution.hpp>
#include <pubgrub/term.hpp>

#include <deque>
#include <initializer_list>
#include <iostream>
#include <set>
#include <variant>
#include <vector>

namespace pubgrub {

// clang-format off
template <typename Provider, requirement Req>
concept provider = requires(const Provider provider, const Req requirement) {
    { provider.best_candidate(requirement) } -> detail::boolean;
    { *provider.best_candidate(requirement) } -> detail::convertible_to<const Req&>;
    { provider.requirements_of(requirement) } -> detail::range_of<Req>;
};
// clang-format on

namespace detail {

template <typename IC>
class ic_record;

template <typename Requirement, typename Allocator>
class ic_record<pubgrub::incompatibility<Requirement, Allocator>> {
    using requirement_type = Requirement;
    using allocator_type   = Allocator;
    using ic_type          = incompatibility<requirement_type, allocator_type>;

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

    using deque_type = std::deque<ic_type, rebind_alloc_t<ic_type>>;
    deque_type _ics{_alloc};

    using ic_by_key_seq_vec = std::vector<ic_by_key_seq, rebind_alloc_t<ic_by_key_seq>>;
    ic_by_key_seq_vec _by_key{_alloc};

    typename ic_by_key_seq_vec::const_iterator _seq_for_key(const key_type& key) const noexcept {
        return std::partition_point(_by_key.begin(), _by_key.end(), [&](const auto& check) {
            return check.key < key;
        });
    }
    typename ic_by_key_seq_vec::iterator _seq_for_key(const key_type& key) noexcept {
        return std::partition_point(_by_key.begin(), _by_key.end(), [&](const auto& check) {
            return check.key < key;
        });
    }

public:
    explicit ic_record(allocator_type ac)
        : _alloc(ac) {}

    const ic_type& record(ic_type&& ic) noexcept {
        const ic_type& new_ic = _ics.emplace_back(std::move(ic));
        for (const term_type& term : new_ic.terms()) {
            auto existing = _seq_for_key(term.key());
            if (existing == _by_key.end()) {
                existing = _by_key.insert(existing, {term.key(), ic_ref_vec{_alloc}});
            }
            existing->ics.push_back(std::cref(new_ic));
        }
        debug_say("Incompatibility: ", new_ic);
        return new_ic;
    }

    const auto& all() const noexcept { return _ics; }

    const auto& for_name(const key_type& key) const noexcept {
        auto seq_iter = _seq_for_key(key);
        assert(seq_iter != _by_key.cend());
        return seq_iter->ics;
    }
};

template <requirement Req, provider<Req> P, typename Allocator>
struct solver {
    using requirement_type = Req;
    using provider_type    = P;
    using allocator_type   = Allocator;
    using ic_type          = incompatibility<requirement_type, allocator_type>;
    using sln_type         = partial_solution<requirement_type, allocator_type>;
    using term_type        = typename ic_type::term_type;
    using key_type         = typename term_type::key_type;

    template <typename T>
    using rebind_alloc = detail::rebind_alloc_t<allocator_type, T>;

    using key_set_type = std::set<key_type, std::less<>, rebind_alloc<key_type>>;
    struct conflict {};
    struct no_conflict {};
    struct almost_conflict {
        const term_type& term;
    };

    using conflict_result = std::variant<conflict, no_conflict, almost_conflict>;

    allocator_type alloc;
    provider_type& provider;

    ic_record<ic_type> ics{alloc};
    key_set_type       changed = key_set_type(rebind_alloc<key_type>(alloc));
    sln_type           sln{alloc};

    void preload_root(requirement_type req) noexcept {
        debug_say("Preloading root requirement: ", req);
        ics.record(ic_type{{term_type{req, false}}, alloc});
        changed.insert(key_of(req));
    }

    auto solve() noexcept {
        for (; !changed.empty(); speculate_one_decision()) {
            unit_propagation();
        }

        debug_say("Solve completed successfully");
        return sln.completed_solution();
    }

    void speculate_one_decision() noexcept {
        debug_say("Speculating the next decision");
        const requirement_type* next_req = sln.next_unsatisfied_term();
        if (!next_req) {
            return;
        }

        debug_say("Next requirement is ", *next_req);

        // Find the best candidate package for the term
        const auto& cand_req = provider.best_candidate(*next_req);
        if (!cand_req) {
            debug_say("Provider has no candidate for requirement ", *next_req);
            assert(false && "Unimplemented: No package can satisfy requirement");
            std::terminate();
            return;
        }

        debug_say("Provider gave us candidate requirement: ", *cand_req);

        auto&& cand_reqs      = provider.requirements_of(*cand_req);
        bool   found_conflict = false;
        for (requirement_type req : cand_reqs) {
            debug_say("Loading incompatibilities for dependency of ", *cand_req, ": ", req);
            const ic_type& new_ic = ics.record(
                ic_type{{term_type{*cand_req}, term_type{std::move(req), false}}, alloc});
            assert(new_ic.terms().size() == 2);
            found_conflict = found_conflict
                || (sln.satisfies(new_ic.terms()[0]) && sln.satisfies(new_ic.terms()[1]));
        }

        if (!found_conflict) {
            debug_say("No existing conflict was found by the new requirements");
            debug_say("!!!!!!!!!!!! Decision: ", *cand_req, " !!!!!!!!!!!!!!!");
            sln.record_decision(term_type{*cand_req}, std::nullopt);
        }

        changed.insert(key_of(*cand_req));
        debug_say("Speculative decision is complete");
    }

    void unit_propagation() noexcept {
        debug_say("Beginning unit propagation");
        while (!changed.empty()) {
            auto next_unit = changed.extract(changed.begin());
            debug_say("Propagate for key: ", next_unit.value());
            propagate_one(next_unit.value());
        }
    }

    void propagate_one(const key_type& key) {
        auto ics_for_name = ics.for_name(key);
        for (const ic_type& ic : ics_for_name) {
            propagate_ic(ic);
        }
    }

    void propagate_ic(const ic_type& ic) {
        debug_say("Propagating incompatibility: ", ic);
        auto res = check_conflict(ic);

        if (auto almost = std::get_if<almost_conflict>(&res)) {
            sln.record_derivation(almost->term.inverse(), ic);
            changed.insert(almost->term.key());
        } else if (std::holds_alternative<conflict>(res)) {
            ic_type conflict_cause = resolve_conflict(ic);
            auto    res2           = check_conflict(conflict_cause);
            auto    almost         = std::get_if<almost_conflict>(&res2);
            assert(almost && "Conflict resolution entered an invalid state");
            sln.record_derivation(almost->term.inverse(), ic);
            changed.insert(almost->term.key());
        } else {
            assert(std::holds_alternative<no_conflict>(res));
            // Nothing to do
        }
    }

    conflict_result check_conflict(const ic_type& ic) const noexcept {
        debug_say("Checking for conflicts with ", ic);
        const term_type* unsat_term = nullptr;
        for (const term_type& term : ic.terms()) {
            auto rel = sln.relation_of(term);
            if (rel == set_relation::disjoint) {
                return no_conflict{};
            } else if (rel == set_relation::overlap) {
                if (unsat_term != nullptr) {
                    return no_conflict{};
                }
                unsat_term = &term;
            } else {
                // Term is satisfied
            }
        }

        if (unsat_term == nullptr) {
            debug_say("Conflict found!");
            return conflict{};
        }

        debug_say("One unsatisfied term: ", *unsat_term);
        return almost_conflict{*unsat_term};
    }

    ic_type resolve_conflict(ic_type ic) noexcept {
        bool ic_changed = false;
        while (true) {
            auto        first_sat_it = sln.first_satisfier_of(ic.terms());
            const auto& satisfier    = *first_sat_it;
            auto        prev_sat     = sln.first_satisfier_of(ic.terms(), satisfier);
            assert(prev_sat < first_sat_it);
            auto prev_sat_level = prev_sat->decision_level;
            if (satisfier.kind == sln_type::assignment_kind::decision
                || prev_sat_level != satisfier.decision_level) {
                if (ic_changed) {
                    ics.record(ic_type(ic));
                }
                sln.backtrack_to(prev_sat_level);
                return ic;
            } else {
                auto prior_cause = build_prior_cause(ic, satisfier.cause, satisfier.term.key());
                assert(false && "Conflict rewrite is not ready.");
            }
        }
    }

    ic_type build_prior_cause(const ic_type&  ic,
                              const ic_type*  prev_sat_cause,
                              const key_type& exclude_key) const noexcept {
        auto copy_filter
            = [&](const term_type& term) { return keys_equivalent(term.key(), exclude_key); };
        using term_alloc = detail::rebind_alloc_t<Allocator, term_type>;
        typename ic_type::term_vec terms{term_alloc(alloc)};
        std::copy_if(ic.terms().begin(),  //
                     ic.terms().end(),
                     std::back_inserter(terms),
                     copy_filter);
        if (prev_sat_cause) {
            std::copy_if(prev_sat_cause->terms().begin(),
                         prev_sat_cause->terms().end(),
                         std::back_inserter(terms),
                         copy_filter);
        }
        return ic_type(std::move(terms));
    }
};

}  // namespace detail

template <requirement_iterator Iter, provider<detail::value_type_t<Iter>> P, typename Allocator>
decltype(auto) solve(Iter it, Iter stop, P&& p, Allocator alloc) {
    detail::debug_say("Beginning new pubgrub solve");
    detail::solver<detail::value_type_t<Iter>, P, Allocator> solver{alloc, p};
    for (; it != stop; ++it) {
        solver.preload_root(*it);
    }
    return solver.solve();
}

template <requirement_iterator Iter, provider<detail::value_type_t<Iter>> P>
decltype(auto) solve(Iter it, Iter stop, P&& p) {
    return solve(it, stop, p, std::allocator<detail::value_type_t<Iter>>());
}

template <requirement_range Container, provider<detail::range_value_type_t<Container>> P>
decltype(auto) solve(Container&& c, P&& p) {
    using std::begin;
    using std::end;
    return solve(begin(c), end(c), p);
}

template <requirement Req, provider<Req> P>
decltype(auto) solve(std::initializer_list<term<Req>> il, P&& p) {
    return solve(il.begin(), il.end(), p);
}

}  // namespace pubgrub