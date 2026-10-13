#include <catch2/catch_test_macros.hpp>

TEST_CASE("Check what Catch2 is work", "[smoke]")
{
    REQUIRE(1 + 1 == 2);
}
