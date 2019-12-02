#pragma once

#include <pubgrub/concepts.hpp>

#include <cassert>
#include <ostream>

namespace pubgrub {

enum class set_relation {
    disjoint,
    overlap,
    subset,
};

template <requirement Requirement>
struct term {
    using requirement_type = Requirement;
    using key_type         = key_type_t<requirement_type>;

    requirement_type requirement;
    bool             positive = true;

    term(requirement_type req, bool b)
        : requirement(std::move(req))
        , positive(b) {}

    explicit term(requirement_type req)
        : term(std::move(req), true) {}

    decltype(auto) key() const noexcept { return key_of(requirement); }

    term inverse() const noexcept { return term{requirement, !positive}; }

    /**
     * Obtain a term that is the logical intersection of the two ranges defined by the terms
     */
    std::optional<term> intersection(const term& other) const noexcept {
        assert(keys_equivalent(key(), other.key())
               && "Cannot perform set operations on terms of differing keys");
        if (positive && other.positive) {
            // Simple case.
            if (auto isect = requirement.intersection(other.requirement)) {
                return term{std::move(*isect), true};
            }
            return std::nullopt;
        }
        if (!positive && !other.positive) {
            // Another simple case. Intersection is all values which do not lie within either range
            auto union_ = requirement.union_(other.requirement);
            if (union_) {
                // Happy path:
                // a: %%%%%%%%%%%%%%--------------%%%%%%%%%%%%%%%%%%
                // b: %%%%%%%%%%%-------------%%%%%%%%%%%%%%%%%%%%%%
                // r: %%%%%%%%%%%-----------------%%%%%%%%%%%%%%%%%%
                return term{std::move(*union_), false};
            }
            // Less happy path...
            // a: %%%%%%%%----------%%%%%%%%%%%%%%%%%%%%%%%%%%%%
            // b: %%%%%%%%%%%%%%%%%%%%%%%%%%-----------%%%%%%%%%
            // r: %%%%%%%%----------%%%%%%%%-----------%%%%%%%%%
            // The above `r` is unrepresentable with a single term, but we _can_ represent the two
            // outer ranges as a single negative range and discard the inner positive range:
            // -- const auto& low  = (std::min)(range().low(), other.range().low());
            // -- const auto& high = (std::max)(range().high(), other.range().high());
            // -- return term{name, range{low, high}, false};
            // But this is still funadmentally incorrect. Assuming the pubgrub
            // algorithm never gets us in such a situation, we'll assume that this
            // path won't be taken in normal code.
            assert(false && "Faulty assumption in the pubgrub impl. This is a BUG!");
            std::terminate();
        }

        if (positive) {
            assert(!other.positive);
            // The intersection of a positive range with a negative one is all areas in the postive
            // range that do not lie within the negative range.
            auto opt = requirement.difference(other.requirement);
            if (opt) {
                // this: ---------%%%%%%%%%------------------------
                // that: %%%%%%%%%%%%--------%%%%%%%%%%%%%%%%%%%%%%
                // both: ---------%%%------------------------------
                return term{std::move(*opt), true};
            }
            return std::nullopt;
        } else {
            assert(!other.positive);
            // term intersection is commutative
            return other.intersection(*this);
        }
    }

    /**
     * Determine if `other` is a subset of `this`. This would mean that `other` _implies_ `this`,
     * that is: every version in `other` is contained within `this`.
     */
    bool implied_by(const term& other) const noexcept {
        if (!keys_equivalent(key(), other.key())) {
            // Unrelated terms cannot imply eachother
            return false;
        }
        if (positive) {
            if (other.positive) {
                if (requirement.implied_by(other.requirement)) {
                    // this: --------%%%%%%%%%%%%%%%%%%%-----------------
                    // that: ------------%%%%%%%%%%%%%-------------------
                    return true;
                } else {
                    // this: ------------%%%%%%%%%%%%%-------------------
                    // that: --------%%%%%%%%%%%%%%%%%%%-----------------
                    return false;
                }
            } else {
                // this: --------%%%%%%%%%%%%%%%%%%%-----------------
                // that: %%%%%%%%%%%%-----------%%%%%%%%%%%%%%%%%%%%%
                // Not possible
                return false;
            }
        } else {
            if (other.positive) {
                if (!requirement.excludes(other.requirement)) {
                    // this: %%%%%%%%%%%%-----------%%%%%%%%%%%%%%%%%%%%%
                    // that: --------%%%%%%%%%%%%%%%%%%%-----------------
                    return false;
                } else {
                    // this: %%%%%%%----------%%%%%%%%%%%%%%%%%%%%%%%%%%%
                    // that: -------------------%%%%%%%%%%%%%%%%---------
                    return true;
                }
            } else {
                // Both negative ranges
                if (requirement.implied_by(other.requirement)) {
                    // this: %%%%%%%----------%%%%%%%%%%%%%%%%%%%%%%%%%%%
                    // that: %%%%%%%%%-------%%%%%%%%%%%%%%%%%%%%%%%%%%%%
                    return false;
                } else {
                    // this: %%%%%%----------%%%%%%%%%%%%%%%%%%%%%%%%%%%%
                    // that: %%%%%----------------%%%%%%%%%%%%%%%%%%%%%%%
                    return true;
                }
            }
        }
    }

    /**
     * Determine whether `this` implies `other`. That is, every version that
     * we contain is also contained within `other`. This is a convenience method
     * for `other.implies(*this)`.
     */
    bool implies(const term& other) const noexcept { return other.implied_by(*this); }

    /**
     * Determine if `other` is completely disjoint with `this`. That is: The
     * two terms share no common versions, and thus cannot be true simultaneously
     */
    bool excludes(const term& other) const noexcept {
        if (!keys_equivalent(key(), other.key())) {
            // Unrelated terms cannot exclude eachother
            return false;
        }
        if (positive) {
            if (other.positive) {
                if (requirement.excludes(other.requirement)) {
                    // this: ---------------%%%%%%%%%%%%%%---------------
                    // that: --%%%%%%%%%%--------------------------------
                    return true;
                } else {
                    // this: --------%%%%%%%%%%%%%%%%%%%-----------------
                    // that: ------------%%%%%%%%%%%%%%%%%%--------------
                    return false;
                }
            } else {
                // Mutual exclusion is reflexive. Deal with the negatives on the left-hand side
                return other.excludes(*this);
            }
        } else {
            if (other.positive) {
                if (requirement.implied_by(other.requirement)) {
                    // this: %%%%%%%%%%%%-----------%%%%%%%%%%%%%%%%%%%%%
                    // that: ---------------%%%%%%%----------------------
                    return true;
                } else {
                    // this: %%%%%%%%%%%%-----------%%%%%%%%%%%%%%%%%%%%%
                    // that: ---------------%%%%%%%%%%%%%----------------
                    return false;
                }
            } else {
                // Impossible for two negative ranges to exclude eachother
                return false;
            }
        }
    }

    set_relation relation_to(const term& other) const noexcept {
        assert(keys_equivalent(key(), other.key()));
        if (implies(other)) {
            return set_relation::subset;
        } else if (excludes(other)) {
            return set_relation::disjoint;
        } else {
            return set_relation::overlap;
        }
    }

    friend std::ostream& operator<<(std::ostream& out, const term& self) noexcept {
        out << "[";
        if (!self.positive) {
            out << "not ";
        }
        out << self.requirement << "]";
        return out;
    }
};

}  // namespace pubgrub