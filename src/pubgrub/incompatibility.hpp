#pragma once

#include <pubgrub/concepts.hpp>
#include <pubgrub/term.hpp>

#include <initializer_list>
#include <numeric>
#include <ostream>

namespace pubgrub {

template <requirement Requirement, typename Allocator = std::allocator<Requirement>>
class incompatibility {
public:
    using term_type           = term<Requirement>;
    using allocator_type      = Allocator;
    using term_allocator_type = detail::rebind_alloc_t<allocator_type, term_type>;
    using term_vec            = std::vector<term_type, term_allocator_type>;

private:
    term_vec _terms;

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
    incompatibility() = default;

    template <detail::same_as<allocator_type> AllocArg>
    explicit incompatibility(AllocArg alloc)
        : _terms(term_allocator_type(alloc)) {}

    incompatibility(std::initializer_list<term_type> terms)
        : incompatibility(terms.begin(), terms.end()) {}

    incompatibility(std::initializer_list<term_type> terms, allocator_type alloc)
        : incompatibility(terms.begin(), terms.end(), alloc) {}

    template <detail::decays_to<term_vec> VecArg>
    explicit incompatibility(VecArg&& arg)
        : _terms(std::forward<VecArg>(arg)) {
        _coalesce();
    }

    template <detail::iterator_of<term_type> Iter>
    incompatibility(Iter it, Iter stop)
        : incompatibility(it, stop, allocator_type()) {}

    template <detail::iterator_of<term_type> Iter>
    incompatibility(Iter it, Iter stop, allocator_type alloc)
        : _terms(it, stop, alloc) {
        _coalesce();
    }

    const term_vec& terms() const noexcept { return _terms; }

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