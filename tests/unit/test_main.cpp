#include <catch2/catch_test_macros.hpp>

#include <yaxi/routex-client.h>

TEST_CASE("public type aliases are usable", "[smoke]") {
    yaxi::ConnectionId id = "connection-96386142-60e5-4ca9-abcf-944efce5bc1e";
    REQUIRE(id == "connection-96386142-60e5-4ca9-abcf-944efce5bc1e");

    yaxi::TraceId trace{0x01, 0x02, 0x03};
    REQUIRE(trace.size() == 3);
}
