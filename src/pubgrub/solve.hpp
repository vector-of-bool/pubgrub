#pragma once

#include <pubgrub/concepts.hpp>
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

template <typename Provider, typename Req>
concept provider = requirement<Req> && requires(const Provider provider, const Req requirement) {
    { provider.best_candidate(requirement) } -> detail::boolean;
    { *provider.best_candidate(requirement) } -> std::convertible_to<const Req&>;
    { provider.requirements_of(requirement) } -> detail::range_of<Req>;
};

static_assert(true);  // Magically keeps clang-format from indenting the rest of the file (??)

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

    typename ic_by_key_seq_vec::const_iterator _seq_for_key(const key_type& key_) const noexcept {
        return sr::partition_point(_by_key, NEO_TL(_1.key < key_));
    }
    typename ic_by_key_seq_vec::iterator _seq_for_key(const key_type& key_) noexcept {
        return sr::partition_point(_by_key, NEO_TL(_1.key < key_));
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

    void preload_root(requirement_type req) noexcept {
        ics.emplace_record(std::vector{term_type{req, false}},
                           alloc,
                           typename ic_type::root_cause{});
        changed.insert(key_of(req));
    }

    auto solve() {
        for (; !changed.empty(); speculate_one_decision()) {
            unit_propagation();
        }

        return sln.completed_solution();
    }

    void speculate_one_decision() {
        const requirement_type* next_req = sln.next_unsatisfied_term();
        if (!next_req) {
            return;
        }

        // Find the best candidate package for the term
        const auto& cand_req = provider.best_candidate(*next_req);
        if (!cand_req) {
            ics.emplace_record(std::vector{term_type{*next_req, true}},
                               alloc,
                               typename ic_type::unavailable_cause{});
            changed.insert(key_of(*next_req));
            return;
        }

        auto&& cand_reqs      = provider.requirements_of(*cand_req);
        bool   found_conflict = false;
        for (requirement_type req : cand_reqs) {
            if (key_of(req) == key_of(*cand_req)) {
                throw std::runtime_error("Package cannot depend on itself.");
            }
            const ic_type& new_ic
                = ics.emplace_record(std::vector{term_type{*cand_req},
                                                 term_type{std::move(req), false}},
                                     alloc,
                                     typename ic_type::dependency_cause{});
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
        auto res = check_conflict(ic);

        if (auto almost = std::get_if<almost_conflict>(&res)) {
            sln.record_derivation(almost->term.inverse(), ic);
            changed.insert(almost->term.key());
        } else if (std::holds_alternative<conflict>(res)) {
            // The cause of the conflict
            const ic_type& root_cause = resolve_conflict(ic);
            auto           res2       = check_conflict(root_cause);
            auto           almost2    = std::get_if<almost_conflict>(&res2);
            assert(almost2 && "Conflict resolution entered an invalid state");
            sln.record_derivation(almost2->term.inverse(), root_cause);
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

    const ic_type& resolve_conflict(std::reference_wrapper<const ic_type> ic_) {
        while (true) {
            const ic_type& ic          = ic_;
            const auto&    opt_bt_info = sln.build_backtrack_info(ic.terms());
            if (!opt_bt_info) {
                // There is nowhere left to backtrack to: There is no possible
                // solution!
                ics.throw_failure(ic);
            }
            const auto& [term, satisfier, prev_sat_level, difference] = *opt_bt_info;
            if (satisfier.is_decision() || prev_sat_level < satisfier.decision_level) {
                sln.backtrack_to(prev_sat_level);
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

                ic_ = ics.emplace_record(std::move(new_terms),
                                         alloc,
                                         conflict_cause_type{ic, *satisfier.cause});
                assert(std::holds_alternative<conflict>(check_conflict(ic_)));
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

template <requirement_range Range, provider<std::ranges::range_value_t<Range>> P>
decltype(auto) solve(Range&& c, P&& p) {
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