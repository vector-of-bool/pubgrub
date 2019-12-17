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
    using term_vec            = std::deque<term_type, term_allocator_type>;

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
        std::sort(_terms.begin(), _terms.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.key() < rhs.key();
        });
        auto walk = _terms.begin();
        while (walk != _terms.end()) {
            walk = _coalesce_at(walk);
        }
    }

    auto _coalesce_at(typename term_vec::iterator pos) noexcept {
        auto subseq_end = std::find_if(pos, _terms.end(), [&](const auto& term) {
            return pos->key() != term.key();
        });
        assert(pos != subseq_end);
        *pos = std::accumulate(std::next(pos),
                               subseq_end,
                               *pos,
                               [](const term_type& lhs, const term_type& rhs) -> term_type {
                                   assert(keys_equivalent(lhs.key(), rhs.key()));
                                   auto un = lhs.intersection(rhs);
                                   assert(un);
                                   return *un;
                               });
        return _terms.erase(std::next(pos), subseq_end);
    }

public:
    incompatibility()                       = default;
    incompatibility(const incompatibility&) = delete;
    incompatibility& operator=(const incompatibility&) = delete;

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

    friend std::ostream& operator<<(std::ostream& out, const incompatibility& ic) noexcept {
        out << "{";
        auto&      terms = ic.terms();
        auto       it    = terms.cbegin();
        const auto end   = terms.cend();
        while (it != end) {
            out << *it;
            ++it;
            if (it != end) {
                out << " ∩ ";
            }
        }
        out << "}";
        return out;
    }
};

}  // namespace pubgrub