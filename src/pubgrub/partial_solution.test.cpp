#include "./partial_solution.hpp"

#include <pubgrub/test_util.hpp>

#include <catch2/catch.hpp>

TEST_CASE("Create a basic partial solution") {
    pubgrub::partial_solution<pubgrub::test::simple_req> sln;
    // Check that move operators compile:
    auto sln2 = std::move(sln);
    sln       = std::move(sln2);
}

TEST_CASE("Record a basic term") {
    pubgrub::partial_solution<pubgrub::test::simple_req> sln;

    using pubgrub::test::simple_term;
    sln.record_derivation(simple_term{{"foo", {5, 6}}}, std::nullopt);  // Record foo=5

    CHECK_FALSE(sln.satisfies(simple_term{{"foo", {4, 5}}}));
    CHECK_FALSE(sln.satisfies(simple_term{{"foo", {12, 13}}}));
    CHECK(sln.satisfies(simple_term{{"foo", {5, 6}}}));
}
