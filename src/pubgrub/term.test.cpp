#include <pubgrub/term.hpp>

#include <pubgrub/test_util.hpp>

#include <catch2/catch.hpp>

// static_assert(pubgrub::requirement<req>);

template <pubgrub::requirement R>
void foo(const R&) {}

TEST_CASE("basic") {
    struct case_ {
        pubgrub::test::simple_version_range a;
        pubgrub::test::simple_version_range b;
        bool                                expect_implies;
        bool                                inverse_implies;
        bool                                expect_excludes;
    };

    auto [range_a, range_b, expect_implies, inverse_implies, expect_excludes]
        = GENERATE(Catch::Generators::values<case_>({
            {{1, 2}, {3, 4}, false, false, true},
            {{1, 2}, {2, 3}, false, false, true},
            {{1, 2}, {1, 3}, true, false, false},
            {{1, 2}, {1, 2}, true, true, false},
            {{1, 3}, {1, 2}, false, true, false},
        }));

    INFO("Check range " << range_a << " against " << range_b);

    pubgrub::test::simple_req req_a{"foo", range_a};
    pubgrub::test::simple_req req_b{"foo", range_b};

    pubgrub::test::simple_term a{req_a};
    pubgrub::test::simple_term b{req_b};

    CHECK(a.implies(b) == expect_implies);
    CHECK(b.implies(a) == inverse_implies);
    CHECK(a.excludes(b) == expect_excludes);
    CHECK(b.excludes(a) == expect_excludes);
}
