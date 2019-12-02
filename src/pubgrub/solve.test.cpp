#include "./solve.hpp"

#include <pubgrub/term.hpp>
#include <pubgrub/test_util.hpp>

#include <catch2/catch.hpp>

#include <algorithm>

using test_term = pubgrub::term<pubgrub::test::simple_req>;

struct test_package {
    std::string                            name;
    int                                    version;
    std::vector<pubgrub::test::simple_req> requirements;
};

struct test_repo {
    std::vector<test_package> packages;

    std::optional<pubgrub::test::simple_req>
    best_candidate(const pubgrub::test::simple_req& req) const noexcept {
        auto max_version
            = std::find_if(packages.rbegin(), packages.rend(), [&](const test_package& pkg) {
                  return (pkg.name == req.key)
                      && (pkg.version >= req.range.low && pkg.version < req.range.high);
              });
        if (max_version == packages.rend()) {
            return std::nullopt;
        }
        return pubgrub::test::simple_req{max_version->name,
                                         pubgrub::test::simple_version_range{max_version->version,
                                                                             max_version->version
                                                                                 + 1}};
    }

    std::vector<pubgrub::test::simple_req>
    requirements_of(const pubgrub::test::simple_req& req) const noexcept {
        const auto& [name, range] = req;
        const auto version        = range.low;
        for (const test_package& pkg : packages) {
            if (pkg.name == name && pkg.version == version) {
                std::vector<pubgrub::test::simple_req> ret;
                return pkg.requirements;
            }
        }
        assert(false && "Impossible?");
    }
};

template <pubgrub::provider<test_term> P>
void foo(P&&) {}

using sln_type = std::vector<test_term>;

// TEST_CASE("Basic solve of empty requirements") {
//     foo(test_provider{});
//     test_repo repo{{}};
//     auto      sln = pubgrub::solve(std::array<test_term, 0>{}, repo);

//     CHECK(sln == sln_type());
// }

// TEST_CASE("Solve for a single requirement") {
//     test_repo repo{{
//         {"foo", 1, {}},
//     }};
//     test_term roots[] = {
//         test_term{{"foo", {1, 2}}},
//     };
//     auto sln = pubgrub::solve(roots, repo);
//     CHECK(sln == sln_type({test_term{{"foo", {1, 2}}}}));
// }

struct solve_case {
    std::string                            name;
    test_repo                              repo;
    std::vector<pubgrub::test::simple_req> roots;
    std::vector<pubgrub::test::simple_req> expected_sln;
};

auto test_case(std::string                            descr,
               test_repo                              repo,
               std::vector<pubgrub::test::simple_req> roots,
               std::vector<pubgrub::test::simple_req> expected_sln) {
    return solve_case{descr, repo, roots, expected_sln};
}

template <typename... Packages>
auto repo(Packages&&... pkgs) {
    return test_repo{std::vector<test_package>({pkgs...})};
}

auto pkg(std::string name, int version, std::vector<pubgrub::test::simple_req> deps) {
    return test_package{name, version, deps};
}

auto req(std::string name, pubgrub::test::simple_version_range range) {
    return pubgrub::test::simple_req{name, range};
}

template <typename... Items>
auto reqs(Items... t) {
    return std::vector<pubgrub::test::simple_req>({t...});
}

template <typename... Terms>
auto sln(Terms... t) {
    return reqs(t...);
}

TEST_CASE("Solving") {
    const solve_case& test = GENERATE(Catch::Generators::values<solve_case>({
        test_case("Empty", repo(), reqs(), sln()),
        test_case("Simple package, no ranges",
                  repo(pkg("foo", 1, {})),
                  reqs(req("foo", {1, 2})),
                  sln(req("foo", {1, 2}))),
        test_case("Single package, simple range",
                  repo(pkg("foo", 2, {})),
                  reqs(req("foo", {1, 3})),
                  sln(req("foo", {2, 3}))),
        test_case("Single package, range requirement, multiple candidates",
                  repo(pkg("foo", 1, {}),  //
                       pkg("foo", 2, {}),
                       pkg("foo", 3, {}),
                       pkg("foo", 4, {})),
                  reqs(req("foo", {1, 6})),
                  sln(req("foo", {4, 5}))),
        test_case("Simple transitive requirement",
                  repo(pkg("foo", 1, {req("bar", {3, 4})}), pkg("bar", 3, {})),
                  reqs(req("foo", {1, 2})),
                  sln(req("foo", {1, 2}), req("bar", {3, 4}))),
        test_case("Multiple transitive requirements",
                  repo(pkg("foo",
                           1,
                           {
                               req("bar", {3, 6}),
                               req("baz", {5, 23}),
                           }),
                       pkg("bar", 5, {}),
                       pkg("baz", 7, {})),
                  reqs(req("foo", {1, 2})),
                  sln(req("foo", {1, 2}),  //
                      req("bar", {5, 6}),
                      req("baz", {7, 8}))),
        test_case("Basic backtracking",
                  // For this test case, we check that backtracking works.
                  // Here's the steps the algorithm _should_ take:
                  // 1. Select foo=1
                  // 2. Select bar=3 as depended on by foo=1
                  // 3. Select baz=6 as depended on by foo=1
                  // 4. See conflict: baz=6 wants bar=4, but we've selected bar=3
                  // 5. Backtrack
                  repo(pkg("foo",
                           1,
                           {
                               req("bar", {1, 6}),
                               req("baz", {3, 8}),
                           }),
                       pkg("bar", 3, {}),
                       pkg("bar", 4, {}),
                       pkg("baz", 6, {req("bar", {4, 5})})),
                  reqs(req("foo", {1, 2})),
                  sln(req("foo", {1, 2}),  //
                      req("bar", {4, 5}),
                      req("baz", {6, 7}))),
        test_case("Simple interdependencies",
                  repo(pkg("a",
                           1,
                           {
                               req("aa", {1, 2}),
                               req("ab", {1, 2}),
                           }),
                       pkg("b",
                           1,
                           {
                               req("ba", {1, 2}),
                               req("bb", {1, 2}),
                           }),
                       pkg("aa", 1, {}),
                       pkg("ab", 1, {}),
                       pkg("ba", 1, {}),
                       pkg("bb", 1, {})),
                  reqs(req("a", {1, 2}),  //
                       req("b", {1, 2})),
                  sln(req("a", {1, 2}),
                      req("aa", {1, 2}),
                      req("ab", {1, 2}),
                      req("b", {1, 2}),
                      req("ba", {1, 2}),
                      req("bb", {1, 2}))),
        test_case("Simple overlapping",
                  repo(pkg("a", 1, {req("shared", {200, 400})}),
                       pkg("b", 1, {req("shared", {300, 500})}),
                       pkg("shared", 200, {}),
                       pkg("shared", 299, {}),
                       pkg("shared", 369, {}),
                       pkg("shared", 400, {}),
                       pkg("shared", 500, {})),
                  reqs(req("a", {1, 2}), req("b", {1, 2})),
                  sln(req("a", {1, 2}), req("b", {1, 2}), req("shared", {369, 370}))),
        test_case("Shared deps with interdependent versioning (backtracking)",
                  repo(pkg("foo", 100, {}),
                       pkg("foo", 101, {req("bang", {100, 101})}),
                       pkg("foo", 102, {req("whoop", {100, 101})}),
                       pkg("foo", 103, {req("zoop", {100, 101})}),
                       pkg("bar", 100, {req("foo", {0, 102})}),
                       pkg("bang", 100, {}),
                       pkg("whoop", 100, {}),
                       pkg("zoop", 100, {})),
                  reqs(req("foo", {0, 103}), req("bar", {100, 101})),
                  sln(req("bar", {100, 101}), req("foo", {101, 102}), req("bang", {100, 101}))),
    }));

    INFO("Checking solve case: " << test.name)
    auto sln = pubgrub::solve(test.roots, test.repo);
    CHECK(sln == test.expected_sln);
}
