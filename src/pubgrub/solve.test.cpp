#include "./solve.hpp"

#include <pubgrub/term.hpp>
#include <pubgrub/test_util.hpp>

#include <catch2/catch.hpp>

#include <algorithm>
#include <sstream>

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
                  return pkg.name == req.key && req.range.contains(pkg.version);
              });
        if (max_version == packages.rend()) {
            return std::nullopt;
        }
        return pubgrub::test::simple_req{max_version->name,
                                         pubgrub::interval_set<int>{max_version->version,
                                                                    max_version->version + 1}};
    }

    std::vector<pubgrub::test::simple_req>
    requirements_of(const pubgrub::test::simple_req& req) const noexcept {
        const auto& [name, range] = req;
        const auto version        = (*range.iter_intervals().begin()).low;
        for (const test_package& pkg : packages) {
            if (pkg.name == name && pkg.version == version) {
                std::vector<pubgrub::test::simple_req> ret;
                return pkg.requirements;
            }
        }
        assert(false && "Impossible?");
        std::terminate();
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

auto req(std::string name, pubgrub::interval_set<int> range) {
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

TEST_CASE("Basic solving") {
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
        test_case("Circular requirements",
                  repo(pkg("foo", 1, {req("bar", {1, 2})}), pkg("bar", 1, {req("foo", {1, 2})})),
                  reqs(req("foo", {1, 2})),
                  sln(req("foo", {1, 2}), req("bar", {1, 2}))),
    }));

    INFO("Checking solve case: " << test.name);
    auto sln = pubgrub::solve(test.roots, test.repo);
    CHECK(sln == test.expected_sln);
}

TEST_CASE("Advanced backtracking") {
    const solve_case& test = GENERATE(Catch::Generators::values<solve_case>({
        test_case("Circular dep with older version",
                  repo(pkg("a", 1, {}),
                       pkg("a", 2, {req("b", {1, 2})}),
                       pkg("b", 1, {req("a", {1, 2})})),
                  reqs(req("a", {1, 1000})),
                  sln(req("a", {1, 2}))),
        test_case("Diamond dependency",
                  repo(pkg("a", 100, {}),
                       pkg("a", 200, {req("c", {100, 200})}),

                       pkg("b", 100, {req("c", {200, 300})}),
                       pkg("b", 200, {req("c", {300, 400})}),

                       pkg("c", 100, {}),
                       pkg("c", 200, {}),
                       pkg("c", 300, {})),
                  reqs(req("a", {1, 1000}),  //
                       req("b", {1, 1000})),
                  sln(req("a", {100, 101}),  //
                      req("b", {200, 201}),
                      req("c", {300, 301}))),
        test_case("Backtrack after partial satisfier",
                  repo(pkg("a", 100, {req("x", {100, 1000})}),
                       pkg("b", 100, {req("x", {1, 200})}),

                       pkg("c", 100, {}),
                       pkg("c", 200, {req("a", {1, 1000}), req("b", {1, 1000})}),

                       pkg("x", 1, {}),
                       pkg("x", 100, {req("y", {100, 101})}),
                       pkg("x", 200, {}),

                       pkg("y", 100, {}),
                       pkg("y", 200, {})),
                  reqs(req("c", {1, 1000}), req("y", {200, 1000})),
                  sln(req("c", {100, 101}), req("y", {200, 201}))),
        test_case("Roll back leaves first",
                  repo(pkg("a", 100, {req("b", {1, 1000})}),
                       pkg("a", 200, {req("b", {1, 1000}), req("c", {200, 201})}),
                       pkg("b", 100, {}),
                       pkg("b", 200, {req("c", {100, 101})}),
                       pkg("c", 100, {}),
                       pkg("c", 200, {})),
                  reqs(req("a", {1, 1000})),
                  sln(req("a", {200, 201}), req("b", {100, 101}), req("c", {200, 201}))),
        test_case("Simple transitive",
                  repo(pkg("foo", 100, {req("bar", {100, 101})}),
                       pkg("foo", 200, {req("bar", {200, 201})}),
                       pkg("foo", 300, {req("bar", {300, 301})}),

                       pkg("bar", 100, {req("baz", {1, 1000})}),
                       pkg("bar", 200, {req("baz", {200, 201})}),
                       pkg("bar", 300, {req("baz", {300, 301})}),

                       pkg("baz", 100, {})),
                  reqs(req("foo", {1, 1000})),
                  sln(req("foo", {100, 101}), req("bar", {100, 101}), req("baz", {100, 101}))),
        test_case("Backtrack to nearer unsatisfied",
                  repo(pkg("a", 100, {req("c", {100, 101})}),
                       pkg("a", 200, {req("c", {200, 201})}),  // c=200 does not exist
                       pkg("b", 100, {}),
                       pkg("b", 200, {}),
                       pkg("b", 300, {}),
                       pkg("c", 100, {})),
                  reqs(req("a", {1, 1000}), req("b", {1, 1000})),
                  sln(req("a", {100, 101}), req("b", {300, 301}), req("c", {100, 101}))),
        test_case("Backtrack over disjoint",
                  repo(pkg("a", 100, {req("foo", {1, 1000})}),
                       pkg("a", 200, {req("foo", {1, 100})}),
                       pkg("foo", 200, {}),
                       pkg("foo", 201, {}),
                       pkg("foo", 202, {}),
                       pkg("foo", 203, {}),
                       pkg("foo", 204, {})),
                  reqs(req("a", {1, 1000}), req("foo", {200, 1000})),
                  sln(req("a", {100, 101}), req("foo", {204, 205}))),
    }));

    INFO("Checking solve case: " << test.name);
    auto sln = pubgrub::solve(test.roots, test.repo);
    CHECK(sln == test.expected_sln);
}

TEST_CASE("Unsolvable") {
    const solve_case& test = GENERATE(Catch::Generators::values<solve_case>({
        test_case("No version matching direct requirement",
                  repo(pkg("foo", 200, {}), pkg("foo", 213, {})),
                  reqs(req("foo", {100, 200})),
                  sln()),
        test_case("No version of shared requirement matching combined constraints",
                  repo(pkg("foo", 100, {req("shared", {200, 300})}),
                       pkg("bar", 100, {req("shared", {290, 400})}),
                       pkg("shared", 250, {}),
                       pkg("shared", 350, {})),
                  reqs(req("foo", {0, 9999}), req("bar", {0, 9999})),
                  sln()),
        test_case("Disjoin constraints fail",
                  repo(pkg("foo", 100, {req("shared", {0, 201})}),
                       pkg("bar", 100, {req("shared", {301, 999})}),
                       pkg("shared", 200, {}),
                       pkg("shared", 400, {})),
                  reqs(req("foo", {100, 101}), req("bar", {100, 101})),
                  sln()),
        test_case("Disjoint root constraints",
                  repo(pkg("foo", 100, {}), pkg("foo", 200, {})),
                  reqs(req("foo", {100, 101}), req("foo", {200, 201})),
                  sln()),
        test_case("Overlapping constraints resolve to unsolvable package",
                  repo(pkg("foo", 100, {req("shared", {100, 300})}),
                       pkg("bar", 100, {req("shared", {200, 400})}),
                       pkg("shared", 150, {}),
                       pkg("shared", 350, {}),
                       pkg("shared", 250, {req("nonesuch", {100, 200})})),
                  reqs(req("foo", {100, 101}), req("bar", {100, 101})),
                  sln()),
        test_case("Overlapping constraints result in transitive incompatibility",
                  repo(pkg("foo", 1, {req("asdf", {100, 300})}),
                       pkg("bar", 100, {req("jklm", {200, 400})}),
                       pkg("asdf", 200, {req("baz", {3, 4})}),
                       pkg("jklm", 300, {req("baz", {4, 5})}),
                       pkg("baz", 3, {}),
                       pkg("baz", 4, {})),
                  reqs(req("foo", {1, 2}), req("bar", {100, 200})),
                  sln()),
        test_case("Unsolvable",
                  repo(pkg("a", 100, {req("b", {100, 101})}),
                       pkg("a", 200, {req("b", {200, 201})}),
                       pkg("b", 100, {req("a", {200, 201})}),
                       pkg("b", 200, {req("a", {100, 101})})),
                  reqs(req("a", {0, 999}), req("b", {0, 999})),
                  sln()),
    }));

    INFO("Checking unsolvable case: " << test.name);
    using exception_type = pubgrub::solve_failure_type_t<pubgrub::test::simple_req>;
    try {
        pubgrub::solve(test.roots, test.repo);
        FAIL("Expected a solver failure");
    } catch (const exception_type& fail) {
        pubgrub::generate_explaination(fail, [&](auto&&) {});
    }
}

struct explain_handler {
    std::stringstream message;

    void say(pubgrub::explain::dependency<pubgrub::test::simple_req> dep) {
        message << dep.dependent << " requires " << dep.dependency;
    }

    void say(pubgrub::explain::disallowed<pubgrub::test::simple_req> dep) {
        message << dep.requirement << " is not allowed";
    }

    void say(pubgrub::explain::unavailable<pubgrub::test::simple_req> un) {
        message << un.requirement << " is not available";
    }

    void say(pubgrub::explain::needed<pubgrub::test::simple_req> need) {
        message << need.requirement << " is needed";
    }

    void say(pubgrub::explain::conflict<pubgrub::test::simple_req> conf) {
        message << conf.a << " conflicts with " << conf.b;
    }

    void say(pubgrub::explain::compromise<pubgrub::test::simple_req> comp) {
        message << comp.left << " and " << comp.right << " aggree on " << comp.result;
    }

    void say(pubgrub::explain::no_solution) { message << "There is no solution"; }

    void operator()(pubgrub::explain::separator) { message << '\n'; }

    template <typename What>
    void operator()(pubgrub::explain::conclusion<What> c) {
        message << "Thus: ";
        say(c.value);
        message << '\n';
    }

    template <typename What>
    void operator()(pubgrub::explain::premise<What> c) {
        message << "Known: ";
        say(c.value);
        message << '\n';
    }
};

TEST_CASE("Explain 1") {
    auto test = test_case("No version matching direct requirement",
                          repo(pkg("foo", 200, {}), pkg("foo", 213, {})),
                          reqs(req("foo", {100, 200})),
                          sln());
    try {
        pubgrub::solve(test.roots, test.repo);
        FAIL("Expected a failure");
    } catch (const pubgrub::solve_failure_type_t<pubgrub::test::simple_req>& fail) {
        explain_handler ex;
        pubgrub::generate_explaination(fail, ex);
        CHECK(ex.message.str() == "Known: foo [100, 200) is not available\n"
                                  "Known: foo [100, 200) is needed\n"
                                  "Thus: There is no solution\n");
    }
}