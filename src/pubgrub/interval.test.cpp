#include "./interval.hpp"

#include <catch2/catch.hpp>

TEST_CASE("Create a simple interval") { pubgrub::interval_set<int> iv{1, 2}; }

TEST_CASE("Intervals contain other intervals") {
    pubgrub::interval_set<int> iv1{1, 10};
    pubgrub::interval_set<int> iv2{4, 6};
    CHECK_FALSE(iv1.contains(10));
    CHECK(iv1.contains(1));
    CHECK(iv1.contains(iv2));
    CHECK_FALSE(iv2.contains(iv1));
    iv1 = {6, 8};
    iv2 = {6, 8};
    CHECK(iv1.contains(iv2));
    CHECK(iv2.contains(iv1));

    iv1 = {300, 301};
    iv2 = {300, 301};
    CHECK(iv1.contains(iv2));
    CHECK(iv2.contains(iv1));
}

TEST_CASE("Intervals can exclude other intervals") {
    pubgrub::interval_set<int> iv1{1, 20};
    pubgrub::interval_set<int> iv2{20, 40};
    CHECK(iv1.disjoint(iv2));
    CHECK(iv2.disjoint(iv1));

    iv1 = {2, 7};
    iv2 = {6, 9};
    CHECK_FALSE(iv1.disjoint(iv2));
}

TEST_CASE("Set operations") {
    using iv_type = pubgrub::interval_set<int>;
    iv_type iv1{1, 10};
    iv_type iv2{3, 7};

    auto un = iv1.union_(iv2);
    CHECK(iv1.contains(un));
    CHECK(un.contains(iv1));
    CHECK(un.num_intervals() == 1);

    auto un2 = un.union_({7, 14});
    CHECK(un2.contains(un));
    CHECK(un2.contains(iv2));
    CHECK_FALSE(iv1.contains(un2));
    CHECK(un2.num_intervals() == 1);
    auto un3 = un.union_({77, 79});
    CHECK(un3.contains(un));
    CHECK(un3.contains(iv1));
    CHECK(un3.contains(iv2));
    CHECK(un3.num_intervals() == 2);

    // Case:
    // %%%%%%%%------%%%%%%%----------
    // ----%%%%%%%%%%%%%%%%%%%%%%%----
    auto iv3 = iv_type{1, 5}.union_({7, 9});
    auto iv4 = iv_type{3, 12};
    un       = iv3.union_(iv4);
    CHECK(un.num_intervals() == 1);
    CHECK(iv_type{1, 12}.contains(un));

    auto diff = iv1.difference(iv2);
    CHECK(iv1.contains(diff));
    CHECK(iv2.disjoint(diff));
    CHECK(diff.num_intervals() == 2);

    auto is = iv1.intersection(iv2);
    CHECK(iv2.contains(is));
    is = iv_type{1, 6}.intersection({5, 9});
    CHECK(iv_type{5, 6}.contains(is));
    CHECK(is.contains(iv_type{5, 6}));

    is = iv_type{1, 2}.intersection({6, 9});
    CHECK(is.num_intervals() == 0);

    is = iv_type{5, 6}.difference({1, 9});
    CHECK(is.num_intervals() == 0);
}