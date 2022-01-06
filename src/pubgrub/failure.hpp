#pragma once

#include <algorithm>
#include <list>
#include <map>
#include <stdexcept>
#include <utility>

#include <pubgrub/incompatibility.hpp>

namespace pubgrub {

class exception_base : public std::runtime_error {
public:
    using runtime_error::runtime_error;
};

class unsolvable_failure_base : public exception_base {
public:
    using exception_base::exception_base;
};

template <typename IC>
class unsolvable_failure : public unsolvable_failure_base {
    std::list<IC> _incompats;

public:
    explicit unsolvable_failure(std::list<IC>&& ics)
        : unsolvable_failure_base("Dependency resolution failed")
        , _incompats(std::move(ics)) {}

    const auto& incompatibilities() const noexcept { return _incompats; }
};

template <requirement Req>
using solve_failure_type_t = unsolvable_failure<incompatibility<Req>>;

template <requirement Req>
struct failure_implication {
    const Req& a;
    const Req& b;
    const Req& implied;
};

namespace explain {

struct no_solution {};

template <requirement Req>
struct dependency {
    const Req& dependent;
    const Req& dependency;
};

template <requirement Req>
struct conflict {
    const Req& a;
    const Req& b;
};

template <requirement Req>
struct disallowed {
    const Req& requirement;
};

template <requirement Req>
struct unavailable {
    const Req& requirement;
};

template <requirement Req>
struct needed {
    const Req& requirement;
};

template <requirement Req>
struct compromise {
    const Req& left;
    const Req& right;
    const Req& result;
};

struct separator {};

template <typename Inner>
struct premise {
    const Inner& value;
};

template <typename Inner>
struct conclusion {
    const Inner& value;
};

// clang-format off
template <typename T, typename Requirement>
concept handler = requires(T h) {
    h(std::declval<conclusion<no_solution>>());
    h(std::declval<conclusion<dependency<Requirement>>>());
    h(std::declval<conclusion<conflict<Requirement>>>());
    h(std::declval<conclusion<disallowed<Requirement>>>());
    h(std::declval<conclusion<unavailable<Requirement>>>());
    h(std::declval<conclusion<needed<Requirement>>>());
    h(separator());
    h(std::declval<premise<dependency<Requirement>>>());
    h(std::declval<premise<conflict<Requirement>>>());
    h(std::declval<premise<disallowed<Requirement>>>());
    h(std::declval<premise<unavailable<Requirement>>>());
    h(std::declval<premise<needed<Requirement>>>());
};
// clang-format on

}  // namespace explain

namespace detail {

template <typename IC, typename Handler>
struct failure_writer {
    using ic_type          = IC;
    using requirement_type = typename ic_type::term_type::requirement_type;

    const unsolvable_failure<ic_type>& failure;

    Handler& handle;

    const std::list<ic_type>& ics = failure.incompatibilities();

    [[noreturn]] void _die() {
        assert(false && "We hit an unknown edge case while generating the dependency resolution error report. Please report this as a bug!");
    }

    bool is_derived(const ic_type& ic) {
        return std::holds_alternative<typename ic_type::conflict_cause>(ic.cause());
    }

    const typename ic_type::conflict_cause& causes_of(const ic_type& ic) {
        assert(is_derived(ic));
        return std::get<typename ic_type::conflict_cause>(ic.cause());
    }

    template <typename Receiver>
    void _transform_ic(const ic_type& ic, Receiver&& r) {
        const auto& terms = ic.terms();
        if (terms.size() == 2) {
            if (terms[0].positive != terms[1].positive) {
                // Two terms where one is negative and the other positive implies a dependency
                // relation, where one requirement implies a requirement of another.
                // The positive term is the dependent, the negative is the dependency
                const auto& [neg, pos] = std::minmax(terms[0], terms[1], [](auto&& t1, auto&& t2) {
                    return t1.positive < t2.positive;
                });
                auto dep = explain::dependency<requirement_type>{pos.requirement, neg.requirement};
                r(dep);
            } else if (terms[0].positive) {
                assert(terms[1].positive);
                auto conflict = explain::conflict<requirement_type>{terms[0].requirement,
                                                                    terms[1].requirement};
                r(conflict);
            } else {
                // Both terms are false. Is this case possible?
                _die();
            }
        } else if (terms.size() == 1) {
            if (terms[0].positive) {
                // A single positive term indicates that the associated requirement has been
                // completely ruled out
                if (std::holds_alternative<typename ic_type::unavailable_cause>(ic.cause())) {
                    auto unavail = explain::unavailable<requirement_type>{terms[0].requirement};
                    r(unavail);
                } else {
                    auto dis = explain::disallowed<requirement_type>{terms[0].requirement};
                    r(dis);
                }
            } else {
                // A single negative term indicates that the requirement is absolute.
                auto need = explain::needed<requirement_type>{terms[0].requirement};
                r(need);
            }
        } else if (terms.size() == 3) {
            if (terms[0].positive and terms[1].positive and not terms[2].positive) {
                r(explain::compromise<requirement_type>{terms[0].requirement,
                                                        terms[1].requirement,
                                                        terms[2].requirement});
            } else {
                _die();
            }
        } else if (terms.size() == 0) {
            r(explain::no_solution());
        } else {
            _die();
        }
    }

    void _send_spacer() { handle(explain::separator()); }

    void _send_conclusion(const ic_type& ic) {
        _transform_ic(ic, [&](auto item) { handle(explain::conclusion<decltype(item)>{item}); });
    }

    void _send_premise(const ic_type& ic) {
        _transform_ic(ic, [&](auto item) { handle(explain::premise<decltype(item)>{item}); });
    }

    void generate() {
        assert(!ics.empty()
               && "Cannot generate an error report from an empty incompatibility list");
        _generate_for(ics.back());
    }

    void _generate_for(const ic_type& ic) {
        if (is_derived(ic)) {
            _generate_for_derived(ic);
        }
    }

    void _generate_for_derived(const ic_type& ic) {
        const auto& [left, right] = causes_of(ic);
        auto left_derived         = is_derived(left);
        auto right_derived        = is_derived(right);
        if (left_derived && right_derived) {
            _generate_complex(ic, left, right);
        } else if (left_derived != right_derived) {
            if (left_derived) {
                _generate_partial(ic, left, right);
            } else {
                _generate_partial(ic, right, left);
            }
        } else {
            _send_premise(left);
            _send_premise(right);
            _send_conclusion(ic);
        }
    }

    void _generate_partial(const ic_type& child, const ic_type& derived, const ic_type& external) {
        const auto& [der_left, der_right] = causes_of(derived);
        bool d_left_derived               = is_derived(der_left);
        bool d_right_derived              = is_derived(der_right);
        if (d_left_derived && !d_right_derived) {
            _generate_for(der_left);
            _send_premise(der_right);
            _send_premise(external);
            _send_conclusion(child);
        } else if (d_right_derived && !d_left_derived) {
            _generate_for(der_right);
            _send_premise(der_left);
            _send_premise(external);
            _send_conclusion(child);
        } else {
            _generate_for(derived);
            _send_premise(external);
            _send_conclusion(child);
        }
    }

    void _generate_complex(const ic_type& child,
                           const ic_type& parent_left,
                           const ic_type& parent_right) {
        const auto& [l_left, l_right] = causes_of(parent_left);
        const auto& [r_left, r_right] = causes_of(parent_right);
        if (!is_derived(l_left) && !is_derived(l_right)) {
            // `parent_left` is derived from two external incompatibilities
            _generate_for(parent_right);
            _generate_for(parent_left);
            _send_conclusion(child);
        } else if (!is_derived(r_left) && !is_derived(r_right)) {
            // `parent_right` is derived from two external incompatibilities
            _generate_for(parent_left);
            _generate_for(parent_right);
            _send_conclusion(child);
        } else {
            _generate_for(parent_left);
            _send_spacer();
            _generate_for(parent_right);
            _send_spacer();
            _send_premise(parent_left);
            _send_conclusion(child);
        }
    }
};

}  // namespace detail

template <typename IC, explain::handler<typename IC::term_type::requirement_type> Handler>
void generate_explaination(const unsolvable_failure<IC>& fail, Handler&& h) {
    detail::failure_writer<IC, Handler> f{fail, h};
    f.generate();
}

}  // namespace pubgrub