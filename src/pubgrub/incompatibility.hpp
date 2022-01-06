#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/term.hpp>

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <numeric>
#include <ostream>
#include <variant>

namespace pubgrub {

template <requirement Requirement, typename Allocator = std::allocator<Requirement>>
class incompatibility {
public:
    using term_type           = term<Requirement>;
    using allocator_type      = Allocator;
    using term_allocator_type = detail::rebind_alloc_t<allocator_type, term_type>;
    using term_vec            = std::vector<term_type, term_allocator_type>;

    struct root_cause {};
    struct speculation_cause {};
    struct unavailable_cause {};
    struct dependency_cause {};
    struct conflict_cause {
        const incompatibility& left;
        const incompatibility& right;
    };
    using cause_type
        = std::variant<root_cause, unavailable_cause, dependency_cause, conflict_cause>;

private:
    term_vec   _terms;
    cause_type _cause;

    void _coalesce() noexcept {
        std::ranges::sort(_terms, std::less<>{}, pubgrub::key_of);
        auto walk = _terms.begin();
        while (walk != _terms.end()) {
            walk = _coalesce_at(walk);
        }
    }

    auto _coalesce_at(typename term_vec::iterator pos) noexcept {
        auto subseq_end = std::ranges::lower_bound(pos,
                                                   _terms.end(),
                                                   pos->key(),
                                                   std::less_equal<>{},
                                                   pubgrub::key_of);
        assert(pos != subseq_end);
        *pos = std::accumulate(std::next(pos),
                               subseq_end,
                               *pos,
                               [](const term_type& lhs, const term_type& rhs) -> term_type {
                                   assert(lhs.key() == rhs.key());
                                   auto un = lhs.intersection(rhs);
                                   assert(un);
                                   return *un;
                               });
        return _terms.erase(std::next(pos), subseq_end);
    }

public:
    incompatibility()        = default;
    incompatibility& operator=(const incompatibility&) = delete;
#ifdef _MSC_VER
    incompatibility(const incompatibility&) {
        // MSVC 19.23 seems to have an issue with instantiating the copy constructor of std::list
        // unconditionally, despite the code never making use of it. This will then cause a
        // reference to the deleted `incompatibility` copy constructor, but we don't ever actually
        // use it, and we don't want it to ever happen. Workaround: Just defined a copy constructor
        // that asserts and terminates. There should never be a code path that actually executes
        // this copy constructor, but put a helpful assertion message just for safety's sake.
        assert(false
               && "An incompatibility was copied. This isn't supposed to happen. This is a bug.");
        std::terminate();
    }
#else
    incompatibility(const incompatibility&) = delete;
#endif

    incompatibility(std::initializer_list<term_type> terms, allocator_type alloc, cause_type cause)
        : incompatibility(terms.begin(), terms.end(), term_allocator_type(alloc), cause) {}

    template <detail::range_of<term_type> VecArg>
    explicit incompatibility(VecArg&& arg, allocator_type alloc, cause_type cause)
        : incompatibility(arg.begin(), arg.end(), alloc, cause) {}

    template <detail::iterator_of<term_type> Iter>
    incompatibility(Iter it, Iter stop, allocator_type alloc, cause_type c)
        : _terms(it, stop, alloc)
        , _cause(c) {
        _coalesce();
    }

    const term_vec&   terms() const noexcept { return _terms; }
    const cause_type& cause() const noexcept { return _cause; }

    friend void do_repr(auto out, const incompatibility* self) noexcept {
        constexpr bool can_repr_req = decltype(out)::template can_repr<Requirement>;
        if constexpr (can_repr_req) {
            out.type("pubgrub::incompatibility<{}>", out.template repr_type<Requirement>());
        } else {
            out.type("pubgrub::incompatibility<[…]>");
        }
        if (self) {
            out.append("{");
            for (auto it = self->terms().cbegin(); it != self->terms().cend(); ++it) {
                out.append("{}", out.repr_value(*it));
                if (std::next(it) != self->terms().cend()) {
                    out.append(" ∧ ");
                }
            }
            out.append("}");
        }
    }
};

}  // namespace pubgrub