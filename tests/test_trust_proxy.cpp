/**
 * @file test_trust_proxy.cpp
 * @brief Tests for trust proxy support with ipaddr CIDR matching.
 *
 * Tests the compileTrust, proxyAddr, allAddrs functions and their
 * integration with Request properties (ip, ips, protocol, hostname).
 */

#include <gtest/gtest.h>
#include <polycpp/express/express.hpp>

using namespace polycpp;
using namespace polycpp::express;
namespace edetail = polycpp::express::detail;

// ═══════════════════════════════════════════════════════════════════════
// compileTrust Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(CompileTrustTest, BoolFalse) {
    auto fn = edetail::compileTrust(JsonValue(false));
    EXPECT_FALSE(fn("127.0.0.1", 0));
    EXPECT_FALSE(fn("10.0.0.1", 0));
    EXPECT_FALSE(fn("192.168.1.1", 0));
}

TEST(CompileTrustTest, BoolTrue) {
    auto fn = edetail::compileTrust(JsonValue(true));
    EXPECT_TRUE(fn("127.0.0.1", 0));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_TRUE(fn("8.8.8.8", 5));
}

TEST(CompileTrustTest, NullValue) {
    auto fn = edetail::compileTrust(JsonValue());
    EXPECT_FALSE(fn("127.0.0.1", 0));
}

TEST(CompileTrustTest, NumberHops) {
    auto fn = edetail::compileTrust(JsonValue(1.0));
    EXPECT_TRUE(fn("anything", 0));   // hop 0 < 1
    EXPECT_FALSE(fn("anything", 1));  // hop 1 >= 1
    EXPECT_FALSE(fn("anything", 2));  // hop 2 >= 1
}

TEST(CompileTrustTest, NumberTwoHops) {
    auto fn = edetail::compileTrust(JsonValue(2.0));
    EXPECT_TRUE(fn("anything", 0));   // hop 0 < 2
    EXPECT_TRUE(fn("anything", 1));   // hop 1 < 2
    EXPECT_FALSE(fn("anything", 2));  // hop 2 >= 2
}

TEST(CompileTrustTest, NumberZeroHops) {
    auto fn = edetail::compileTrust(JsonValue(0.0));
    EXPECT_FALSE(fn("anything", 0));
}

TEST(CompileTrustTest, StringSingleIP) {
    auto fn = edetail::compileTrust(JsonValue(std::string("10.0.0.1")));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_FALSE(fn("10.0.0.2", 0));
    EXPECT_FALSE(fn("192.168.1.1", 0));
}

TEST(CompileTrustTest, StringCIDR) {
    auto fn = edetail::compileTrust(JsonValue(std::string("10.0.0.0/8")));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_TRUE(fn("10.255.255.255", 0));
    EXPECT_FALSE(fn("11.0.0.1", 0));
    EXPECT_FALSE(fn("192.168.1.1", 0));
}

TEST(CompileTrustTest, StringCIDR16) {
    auto fn = edetail::compileTrust(JsonValue(std::string("192.168.0.0/16")));
    EXPECT_TRUE(fn("192.168.0.1", 0));
    EXPECT_TRUE(fn("192.168.255.255", 0));
    EXPECT_FALSE(fn("192.169.0.1", 0));
    EXPECT_FALSE(fn("10.0.0.1", 0));
}

TEST(CompileTrustTest, StringCommaSeparated) {
    auto fn = edetail::compileTrust(JsonValue(std::string("10.0.0.1, 10.0.0.2")));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_TRUE(fn("10.0.0.2", 0));
    EXPECT_FALSE(fn("10.0.0.3", 0));
}

TEST(CompileTrustTest, StringIPv6Loopback) {
    auto fn = edetail::compileTrust(JsonValue(std::string("::1")));
    EXPECT_TRUE(fn("::1", 0));
    EXPECT_FALSE(fn("::2", 0));
    EXPECT_FALSE(fn("127.0.0.1", 0));
}

TEST(CompileTrustTest, StringIPv6CIDR) {
    auto fn = edetail::compileTrust(JsonValue(std::string("fe80::/10")));
    EXPECT_TRUE(fn("fe80::1", 0));
    EXPECT_TRUE(fn("fe80::abcd:1234", 0));
    EXPECT_FALSE(fn("2001:db8::1", 0));
}

TEST(CompileTrustTest, ArrayOfIPs) {
    JsonArray arr;
    arr.push_back(JsonValue(std::string("10.0.0.1")));
    arr.push_back(JsonValue(std::string("10.0.0.2")));
    auto fn = edetail::compileTrust(JsonValue(arr));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_TRUE(fn("10.0.0.2", 0));
    EXPECT_FALSE(fn("10.0.0.3", 0));
}

TEST(CompileTrustTest, ArrayOfCIDRs) {
    JsonArray arr;
    arr.push_back(JsonValue(std::string("10.0.0.0/8")));
    arr.push_back(JsonValue(std::string("172.16.0.0/12")));
    auto fn = edetail::compileTrust(JsonValue(arr));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_TRUE(fn("172.16.0.1", 0));
    EXPECT_TRUE(fn("172.31.255.255", 0));
    EXPECT_FALSE(fn("192.168.1.1", 0));
    EXPECT_FALSE(fn("8.8.8.8", 0));
}

TEST(CompileTrustTest, NamedRangeLoopback) {
    auto fn = edetail::compileTrust(JsonValue(std::string("loopback")));
    EXPECT_TRUE(fn("127.0.0.1", 0));
    EXPECT_TRUE(fn("127.255.255.255", 0));
    EXPECT_TRUE(fn("::1", 0));
    EXPECT_FALSE(fn("10.0.0.1", 0));
}

TEST(CompileTrustTest, NamedRangeLinklocal) {
    auto fn = edetail::compileTrust(JsonValue(std::string("linklocal")));
    EXPECT_TRUE(fn("169.254.0.1", 0));
    EXPECT_TRUE(fn("169.254.255.255", 0));
    EXPECT_TRUE(fn("fe80::1", 0));
    EXPECT_FALSE(fn("10.0.0.1", 0));
}

TEST(CompileTrustTest, NamedRangeUniquelocal) {
    auto fn = edetail::compileTrust(JsonValue(std::string("uniquelocal")));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_TRUE(fn("172.16.0.1", 0));
    EXPECT_TRUE(fn("192.168.1.1", 0));
    EXPECT_FALSE(fn("8.8.8.8", 0));
}

TEST(CompileTrustTest, InvalidIPReturnsFalse) {
    auto fn = edetail::compileTrust(JsonValue(std::string("10.0.0.0/8")));
    EXPECT_FALSE(fn("not-an-ip", 0));
    EXPECT_FALSE(fn("", 0));
}

// ═══════════════════════════════════════════════════════════════════════
// proxyAddr Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ProxyAddrTest, NoTrustReturnsSocket) {
    auto trust = edetail::compileTrust(JsonValue(false));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1", trust);
    EXPECT_EQ(result, "127.0.0.1");
}

TEST(ProxyAddrTest, TrustAllReturnsLeftmost) {
    auto trust = edetail::compileTrust(JsonValue(true));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    EXPECT_EQ(result, "10.0.0.1");
}

TEST(ProxyAddrTest, TrustOneHop) {
    // addrs = [10.0.0.1, 10.0.0.2, 127.0.0.1]
    // hop 0 = socket (127.0.0.1), trusted (hop 0 < 1)
    // hop 1 = 10.0.0.2, not trusted (hop 1 >= 1)
    // Result: 10.0.0.2
    auto trust = edetail::compileTrust(JsonValue(1.0));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    EXPECT_EQ(result, "10.0.0.2");
}

TEST(ProxyAddrTest, TrustTwoHops) {
    // addrs = [10.0.0.1, 10.0.0.2, 127.0.0.1]
    // hop 0 = socket (127.0.0.1), trusted (0 < 2)
    // hop 1 = 10.0.0.2, trusted (1 < 2)
    // hop 2 = 10.0.0.1, not trusted (2 >= 2)
    // Result: 10.0.0.1
    auto trust = edetail::compileTrust(JsonValue(2.0));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    EXPECT_EQ(result, "10.0.0.1");
}

TEST(ProxyAddrTest, TrustByCIDR) {
    // Socket 10.0.0.1 matches 10.0.0.0/8 -> trusted
    // XFF: "1.2.3.4, 10.0.0.2"
    // addrs = [1.2.3.4, 10.0.0.2, 10.0.0.1]
    // Walk from right: 10.0.0.1 trusted, 10.0.0.2 trusted, 1.2.3.4 not trusted
    // Result: 1.2.3.4
    auto trust = edetail::compileTrust(JsonValue(std::string("10.0.0.0/8")));
    auto result = edetail::proxyAddr("10.0.0.1", "1.2.3.4, 10.0.0.2", trust);
    EXPECT_EQ(result, "1.2.3.4");
}

TEST(ProxyAddrTest, TrustByCIDRPartialChain) {
    // Socket 10.0.0.1 matches 10.0.0.0/8 -> trusted
    // XFF: "1.2.3.4, 5.6.7.8"
    // addrs = [1.2.3.4, 5.6.7.8, 10.0.0.1]
    // Walk from right: 10.0.0.1 trusted, 5.6.7.8 NOT trusted (not in 10.0.0.0/8)
    // Result: 5.6.7.8
    auto trust = edetail::compileTrust(JsonValue(std::string("10.0.0.0/8")));
    auto result = edetail::proxyAddr("10.0.0.1", "1.2.3.4, 5.6.7.8", trust);
    EXPECT_EQ(result, "5.6.7.8");
}

TEST(ProxyAddrTest, NoXFFReturnsSocket) {
    auto trust = edetail::compileTrust(JsonValue(true));
    auto result = edetail::proxyAddr("127.0.0.1", "", trust);
    EXPECT_EQ(result, "127.0.0.1");
}

TEST(ProxyAddrTest, SingleXFFTrusted) {
    auto trust = edetail::compileTrust(JsonValue(true));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1", trust);
    EXPECT_EQ(result, "10.0.0.1");
}

TEST(ProxyAddrTest, TrustSpecificIP) {
    // Trust only 127.0.0.1
    auto trust = edetail::compileTrust(JsonValue(std::string("127.0.0.1")));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    // Socket 127.0.0.1 trusted, next is 10.0.0.2 which is NOT 127.0.0.1
    EXPECT_EQ(result, "10.0.0.2");
}

TEST(ProxyAddrTest, TrustMultipleSpecificIPs) {
    // Trust 127.0.0.1 and 10.0.0.2
    JsonArray arr;
    arr.push_back(JsonValue(std::string("127.0.0.1")));
    arr.push_back(JsonValue(std::string("10.0.0.2")));
    auto trust = edetail::compileTrust(JsonValue(arr));
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    // Socket 127.0.0.1 trusted, 10.0.0.2 trusted, 10.0.0.1 not trusted -> return 10.0.0.1
    EXPECT_EQ(result, "10.0.0.1");
}

// ═══════════════════════════════════════════════════════════════════════
// allAddrs Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(AllAddrsTest, TrustAllReturnsAll) {
    auto trust = edetail::compileTrust(JsonValue(true));
    auto result = edetail::allAddrs("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    // Chain: [10.0.0.1, 10.0.0.2, 127.0.0.1]
    // All trusted, no truncation.
    // Express behavior: reverse then pop (removes leftmost XFF entry).
    // Result: [127.0.0.1, 10.0.0.2]
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "127.0.0.1");
    EXPECT_EQ(result[1], "10.0.0.2");
}

TEST(AllAddrsTest, TrustNoneReturnsEmpty) {
    auto trust = edetail::compileTrust(JsonValue(false));
    auto result = edetail::allAddrs("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    // No addresses trusted: truncates at first entry, reverse+pop yields empty.
    EXPECT_TRUE(result.empty());
}

TEST(AllAddrsTest, TrustOneHop) {
    auto trust = edetail::compileTrust(JsonValue(1.0));
    // Only first hop (index 0) trusted, truncate at index 1.
    auto result = edetail::allAddrs("127.0.0.1", "10.0.0.1, 10.0.0.2", trust);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "10.0.0.2");
}

TEST(AllAddrsTest, TrustByCIDR) {
    auto trust = edetail::compileTrust(JsonValue(std::string("10.0.0.0/8")));
    // First XFF entry (1.2.3.4) is not in 10.0.0.0/8, truncates immediately.
    auto result = edetail::allAddrs("10.0.0.1", "1.2.3.4, 10.0.0.2", trust);
    EXPECT_TRUE(result.empty());
}

TEST(AllAddrsTest, TrustByCIDRFullChain) {
    auto trust = edetail::compileTrust(JsonValue(std::string("10.0.0.0/8")));
    // All addresses in 10.0.0.0/8, no truncation.
    auto result = edetail::allAddrs("10.0.0.1", "10.0.0.3, 10.0.0.2", trust);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "10.0.0.1");
    EXPECT_EQ(result[1], "10.0.0.2");
}

TEST(AllAddrsTest, NoXFF) {
    auto trust = edetail::compileTrust(JsonValue(true));
    // Only socket address in chain; after reverse+pop, empty result.
    auto result = edetail::allAddrs("127.0.0.1", "", trust);
    EXPECT_TRUE(result.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// parseIpNotation Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ParseIpNotationTest, PlainIPv4) {
    auto subnet = edetail::parseIpNotation("10.0.0.1");
    EXPECT_TRUE(std::holds_alternative<polycpp::ipaddr::IPv4>(subnet.ip));
    EXPECT_EQ(subnet.prefix, 32);
}

TEST(ParseIpNotationTest, IPv4CIDR) {
    auto subnet = edetail::parseIpNotation("10.0.0.0/8");
    EXPECT_TRUE(std::holds_alternative<polycpp::ipaddr::IPv4>(subnet.ip));
    EXPECT_EQ(subnet.prefix, 8);
}

TEST(ParseIpNotationTest, PlainIPv6) {
    auto subnet = edetail::parseIpNotation("::1");
    EXPECT_TRUE(std::holds_alternative<polycpp::ipaddr::IPv6>(subnet.ip));
    EXPECT_EQ(subnet.prefix, 128);
}

TEST(ParseIpNotationTest, IPv6CIDR) {
    auto subnet = edetail::parseIpNotation("fe80::/10");
    EXPECT_TRUE(std::holds_alternative<polycpp::ipaddr::IPv6>(subnet.ip));
    EXPECT_EQ(subnet.prefix, 10);
}

TEST(ParseIpNotationTest, InvalidAddress) {
    EXPECT_THROW(edetail::parseIpNotation("not-an-ip"), std::invalid_argument);
}

// ═══════════════════════════════════════════════════════════════════════
// ipMatchesSubnet Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IpMatchesSubnetTest, IPv4Match) {
    auto subnet = edetail::parseIpNotation("10.0.0.0/8");
    EXPECT_TRUE(edetail::ipMatchesSubnet("10.0.0.1", subnet));
    EXPECT_TRUE(edetail::ipMatchesSubnet("10.255.255.255", subnet));
    EXPECT_FALSE(edetail::ipMatchesSubnet("11.0.0.1", subnet));
}

TEST(IpMatchesSubnetTest, IPv6Match) {
    auto subnet = edetail::parseIpNotation("fe80::/10");
    EXPECT_TRUE(edetail::ipMatchesSubnet("fe80::1", subnet));
    EXPECT_FALSE(edetail::ipMatchesSubnet("2001:db8::1", subnet));
}

TEST(IpMatchesSubnetTest, ExactMatch) {
    auto subnet = edetail::parseIpNotation("127.0.0.1");
    EXPECT_TRUE(edetail::ipMatchesSubnet("127.0.0.1", subnet));
    EXPECT_FALSE(edetail::ipMatchesSubnet("127.0.0.2", subnet));
}

TEST(IpMatchesSubnetTest, InvalidAddress) {
    auto subnet = edetail::parseIpNotation("10.0.0.0/8");
    EXPECT_FALSE(edetail::ipMatchesSubnet("not-an-ip", subnet));
    EXPECT_FALSE(edetail::ipMatchesSubnet("", subnet));
}

// ═══════════════════════════════════════════════════════════════════════
// Application trust proxy integration
// ═══════════════════════════════════════════════════════════════════════

TEST(ApplicationTrustProxyTest, DefaultIsFalse) {
    Application app;
    auto& trust = app.trustFunction();
    // Default trust proxy is false — trust function always returns false
    EXPECT_FALSE(trust("127.0.0.1", 0));
}

TEST(ApplicationTrustProxyTest, SetToTrue) {
    Application app;
    app.set("trust proxy", true);
    auto& trust = app.trustFunction();
    EXPECT_TRUE(trust("127.0.0.1", 0));
    EXPECT_TRUE(trust("10.0.0.1", 0));
}

TEST(ApplicationTrustProxyTest, SetToNumber) {
    Application app;
    app.set("trust proxy", 2.0);
    auto& trust = app.trustFunction();
    EXPECT_TRUE(trust("any", 0));
    EXPECT_TRUE(trust("any", 1));
    EXPECT_FALSE(trust("any", 2));
}

TEST(ApplicationTrustProxyTest, SetToCIDR) {
    Application app;
    app.set("trust proxy", std::string("10.0.0.0/8"));
    auto& trust = app.trustFunction();
    EXPECT_TRUE(trust("10.0.0.1", 0));
    EXPECT_FALSE(trust("192.168.1.1", 0));
}

TEST(ApplicationTrustProxyTest, EnableSetsTrue) {
    Application app;
    app.enable("trust proxy");
    auto& trust = app.trustFunction();
    EXPECT_TRUE(trust("127.0.0.1", 0));
}

TEST(ApplicationTrustProxyTest, DisableSetsFalse) {
    Application app;
    app.enable("trust proxy");
    app.disable("trust proxy");
    auto& trust = app.trustFunction();
    EXPECT_FALSE(trust("127.0.0.1", 0));
}

// ═══════════════════════════════════════════════════════════════════════
// Protocol trust proxy tests
// ═══════════════════════════════════════════════════════════════════════

// NOTE: The protocol/hostname/ip/ips tests require constructing
// Request objects with real http::IncomingMessage objects, which is
// difficult to do in unit tests without a running server. The core
// proxy-addr logic is tested above through the detail functions.
// Integration testing with actual HTTP requests would be done in
// end-to-end tests.

// ═══════════════════════════════════════════════════════════════════════
// buildForwardedAddrs Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(BuildForwardedAddrsTest, EmptyXFF) {
    auto result = edetail::buildForwardedAddrs("127.0.0.1", "");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "127.0.0.1");
}

TEST(BuildForwardedAddrsTest, SingleXFF) {
    auto result = edetail::buildForwardedAddrs("127.0.0.1", "10.0.0.1");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "10.0.0.1");
    EXPECT_EQ(result[1], "127.0.0.1");
}

TEST(BuildForwardedAddrsTest, MultipleXFF) {
    auto result = edetail::buildForwardedAddrs("127.0.0.1", "10.0.0.1, 10.0.0.2, 10.0.0.3");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], "10.0.0.1");
    EXPECT_EQ(result[1], "10.0.0.2");
    EXPECT_EQ(result[2], "10.0.0.3");
    EXPECT_EQ(result[3], "127.0.0.1");
}

// ═══════════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════════

TEST(ProxyAddrTest, IPv6LoopbackSocket) {
    auto trust = edetail::compileTrust(JsonValue(std::string("::1")));
    auto result = edetail::proxyAddr("::1", "10.0.0.1", trust);
    // ::1 is trusted, 10.0.0.1 is not ::1, so result is 10.0.0.1
    EXPECT_EQ(result, "10.0.0.1");
}

TEST(ProxyAddrTest, MixedIPv4IPv6Chain) {
    auto trust = edetail::compileTrust(JsonValue(std::string("loopback")));
    // Trust loopback: 127.0.0.0/8 and ::1/128
    auto result = edetail::proxyAddr("127.0.0.1", "10.0.0.1, 127.0.0.2", trust);
    // addrs = [10.0.0.1, 127.0.0.2, 127.0.0.1]
    // Walk from right: 127.0.0.1 trusted, 127.0.0.2 trusted, 10.0.0.1 not trusted
    EXPECT_EQ(result, "10.0.0.1");
}

TEST(CompileTrustTest, ArrayWithNamedRange) {
    JsonArray arr;
    arr.push_back(JsonValue(std::string("loopback")));
    arr.push_back(JsonValue(std::string("10.0.0.1")));
    auto fn = edetail::compileTrust(JsonValue(arr));
    EXPECT_TRUE(fn("127.0.0.1", 0));
    EXPECT_TRUE(fn("::1", 0));
    EXPECT_TRUE(fn("10.0.0.1", 0));
    EXPECT_FALSE(fn("10.0.0.2", 0));
}
