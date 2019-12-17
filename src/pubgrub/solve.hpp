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
template <typename Provider, typename Req>
concept provider =
    requirement<Req>
    &&
requires(const Provider provider, const Req requirement) {
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
            if (existing == _by_key.end() || existing->key != term.key()) {
                existing = _by_key.insert(existing, {term.key(), ic_ref_vec{_alloc}});
            }
            existing->ics.push_back(std::cref(new_ic));
        }
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
        ics.record(ic_type{{term_type{req, false}}, alloc});
        changed.insert(key_of(req));
    }

    auto solve() {
        for (; !changed.empty(); speculate_one_decision()) {
            unit_propagation();
        }

        return sln.completed_solution();
    }

    void speculate_one_decision() noexcept {
        const requirement_type* next_req = sln.next_unsatisfied_term();
        if (!next_req) {
            return;
        }

        // Find the best candidate package for the term
        const auto& cand_req = provider.best_candidate(*next_req);
        if (!cand_req) {
            ics.record(ic_type{{term_type{*next_req, true}}, alloc});
            changed.insert(key_of(*next_req));
            return;
        }

        auto&& cand_reqs      = provider.requirements_of(*cand_req);
        bool   found_conflict = false;
        for (requirement_type req : cand_reqs) {
            const ic_type& new_ic = ics.record(
                ic_type{{term_type{*cand_req}, term_type{std::move(req), false}}, alloc});
            assert(new_ic.terms().size() == 2);
            found_conflict = found_conflict
                || std::all_of(new_ic.terms().cbegin(),
                               new_ic.terms().cend(),
                               [&](const term_type& ic_term) {
                                   return ic_term.key() == key_of(*cand_req)
                                       || sln.satisfies(ic_term);
                               });
        }

        if (!found_conflict) {
            sln.record_decision(term_type{*cand_req});
        }

        changed.insert(key_of(*cand_req));
    }

    void unit_propagation() {
        while (!changed.empty()) {
            auto next_unit = changed.extract(changed.begin());
            propagate_one(next_unit.value());
        }
    }

    void propagate_one(const key_type& key) {
        auto ics_for_name = ics.for_name(key);
        for (const ic_type& ic : ics_for_name) {
            if (!propagate_ic(ic)) {
                break;
            }
        }
    }

    bool propagate_ic(const ic_type& ic) {
        auto res = check_conflict(ic);

        if (auto almost = std::get_if<almost_conflict>(&res)) {
            sln.record_derivation(almost->term.inverse(), ic);
            changed.insert(almost->term.key());
        } else if (std::holds_alternative<conflict>(res)) {
            const ic_type& conflict_cause = resolve_conflict(ic);
            auto           res2           = check_conflict(conflict_cause);
            auto           almost         = std::get_if<almost_conflict>(&res2);
            assert(almost && "Conflict resolution entered an invalid state");
            changed.clear();
            sln.record_derivation(almost->term.inverse(), conflict_cause);
            changed.clear();
            changed.insert(almost->term.key());
            return false;
        } else {
            assert(std::holds_alternative<no_conflict>(res));
            // Nothing to do
        }
        return true;
    }

    conflict_result check_conflict(const ic_type& ic) const noexcept {
        const term_type* unsat_term = nullptr;
        for (const term_type& term : ic.terms()) {
            auto rel = sln.relation_to(term);
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
            return conflict{};
        }

        return almost_conflict{*unsat_term};
    }

    ic_type resolve_conflict(ic_type ic) {
        bool ic_changed = false;
        while (true) {
            // auto&& satisfier                        = *sln.first_satisfier_of(ic.terms());
            // auto prev_sat_level = sln.first_satisfier_of(ic.terms(), satisfier)->decision_level;
            // const auto& [satisfier, prev_sat_level] = sln.build_backtrack_info(ic.terms());
            const auto& [term, satisfier, prev_sat_level, difference]
                = sln.build_backtrack_info_2(ic.terms());
            if (satisfier.is_decision() || prev_sat_level < satisfier.decision_level) {
                sln.backtrack_to(prev_sat_level);
                if (ic_changed) {
                    ics.record(ic_type(ic));
                }
                return ic;
            } else {
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
                    new_terms.push_back(difference->inverse());
                }
                assert(std::all_of(new_terms.cbegin(),
                                   new_terms.cend(),
                                   [&](const term_type& term) { return sln.satisfies(term); }));
                auto ic2 = ic_type(new_terms);
                assert(!std::equal(ic.terms().begin(),
                                   ic.terms().end(),
                                   ic2.terms().begin(),
                                   ic2.terms().end()));
                ic         = ic_type(std::move(new_terms));
                ic_changed = true;
                // assert(std::holds_alternative<conflict>(check_conflict(ic)));
            }
        }
    }

    auto build_prior_cause_term(const ic_type&  ic,
                                const ic_type&  prev_sat_cause,
                                const key_type& exclude_key) const noexcept {
        auto copy_filter
            = [&](const term_type& term) { return !keys_equivalent(term.key(), exclude_key); };
        using term_alloc = detail::rebind_alloc_t<Allocator, term_type>;
        typename ic_type::term_vec terms{term_alloc(alloc)};
        std::copy_if(ic.terms().begin(),  //
                     ic.terms().end(),
                     std::back_inserter(terms),
                     copy_filter);
        std::copy_if(prev_sat_cause.terms().begin(),
                     prev_sat_cause.terms().end(),
                     std::back_inserter(terms),
                     copy_filter);
        assert(std::all_of(ic.terms().cbegin(), ic.terms().cend(), [&](const term_type& term) {
            return sln.satisfies(term);
        }));
        return terms;
    }
};

}  // namespace detail

template <requirement_iterator Iter, provider<detail::value_type_t<Iter>> P, typename Allocator>
decltype(auto) solve(Iter it, Iter stop, P&& p, Allocator alloc) {
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