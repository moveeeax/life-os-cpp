/**
 * @file test_webhooks.cpp
 * @brief Webhook URL guard — the SSRF defense. No infra (no HTTP is made:
 *        rejection happens before any network call).
 */

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "webhooks/Webhooks.hpp"

using json = nlohmann::json;

namespace {

TEST(Webhooks, BlocksInternalAndMetadataHosts) {
    using Webhooks::detail::host_is_blocked;
    using Webhooks::detail::url_host;

    EXPECT_TRUE(host_is_blocked(url_host("http://localhost/hook")));
    EXPECT_TRUE(host_is_blocked(url_host("http://127.0.0.1:8080/hook")));
    EXPECT_TRUE(host_is_blocked(url_host("https://10.0.0.5/hook")));
    EXPECT_TRUE(host_is_blocked(url_host("https://192.168.1.1/")));
    EXPECT_TRUE(host_is_blocked(url_host("http://172.16.0.1/")));
    EXPECT_TRUE(host_is_blocked(url_host("http://172.31.255.255/")));
    // The cloud metadata endpoint — the classic SSRF target.
    EXPECT_TRUE(host_is_blocked(url_host("http://169.254.169.254/latest/meta-data/")));

    EXPECT_FALSE(host_is_blocked(url_host("https://hooks.example.com/x")));
    EXPECT_FALSE(host_is_blocked(url_host("https://172.32.0.1/")));  // just outside 172.16/12

    EXPECT_TRUE(url_host("ftp://internal/x").empty());  // non-http(s) → no host
    EXPECT_TRUE(url_host("not a url").empty());
}

TEST(Webhooks, BlocksEveryInternalAddressForm) {
    using Webhooks::detail::ip_literal_is_blocked;
    for (const char* addr : {"0.0.0.0",
                             "0.1.2.3",
                             "100.64.0.1",
                             "127.0.0.1",
                             "169.254.169.254",
                             "192.0.0.8",
                             "198.18.0.1",
                             "224.0.0.1",
                             "255.255.255.255",
                             "::",
                             "::1",
                             "::ffff:127.0.0.1",
                             "::ffff:10.0.0.1",
                             "::ffff:169.254.169.254",
                             "64:ff9b::a9fe:a9fe",
                             "fe80::1",
                             "fd00::1",
                             "ff02::1",
                             "not-an-ip"}) {
        EXPECT_TRUE(ip_literal_is_blocked(addr)) << addr;
    }
    for (const char* addr : {"8.8.8.8", "1.1.1.1", "172.32.0.1", "2606:4700:4700::1111", "::ffff:8.8.8.8"}) {
        EXPECT_FALSE(ip_literal_is_blocked(addr)) << addr;
    }
}

TEST(Webhooks, HostCheckCoversLiteralsAndBracketedIpv6) {
    using Webhooks::detail::host_is_blocked;
    using Webhooks::detail::url_host;
    EXPECT_TRUE(host_is_blocked(url_host("http://0.0.0.0:9000/")));
    EXPECT_TRUE(host_is_blocked(url_host("http://[::1]:8080/hook")));
    EXPECT_TRUE(host_is_blocked(url_host("http://[::ffff:127.0.0.1]/")));
    EXPECT_TRUE(host_is_blocked(url_host("http://metadata.localhost/")));
    EXPECT_EQ(url_host("https://user:pw@Hooks.Example.com:8443/x?y=1"), "hooks.example.com");
}

TEST(Webhooks, ResolverRefusesNamesThatPointInside) {
    using Webhooks::detail::resolve_public;
    // localhost resolves from /etc/hosts, no network needed.
    EXPECT_THROW(resolve_public("localhost"), std::runtime_error);
    EXPECT_THROW(resolve_public("127.0.0.1"), std::runtime_error);
    EXPECT_THROW(resolve_public("::ffff:10.1.2.3"), std::runtime_error);
    // A numeric public address resolves to itself without a DNS query.
    EXPECT_EQ(resolve_public("8.8.8.8"), "8.8.8.8");
}

TEST(Webhooks, PortAndRedaction) {
    using Webhooks::detail::redact_url;
    using Webhooks::detail::url_port;
    EXPECT_EQ(url_port("https://hooks.example.com/x"), 443);
    EXPECT_EQ(url_port("http://hooks.example.com/x"), 80);
    EXPECT_EQ(url_port("http://hooks.example.com:8080/x"), 8080);
    EXPECT_EQ(url_port("http://[2606:4700::1111]:9443/x"), 9443);
    EXPECT_EQ(url_port("http://hooks.example.com:99999/x"), 0);
    EXPECT_EQ(redact_url("https://user:secret@hooks.example.com:8443/path/x?token=abc#frag"),
              "https://hooks.example.com:8443/path/x");
    EXPECT_EQ(redact_url("https://hooks.example.com?token=abc"), "https://hooks.example.com");
}

TEST(Webhooks, ProcessJobRefusesInternalOrNonHttpUrl) {
    EXPECT_THROW(Webhooks::process_job({{"url", "http://169.254.169.254/"}, {"event", "e"}, {"body", json::object()}}),
                 std::exception);
    EXPECT_THROW(Webhooks::process_job({{"url", "ftp://nope/"}, {"event", "e"}}), std::exception);
    EXPECT_THROW(Webhooks::process_job({{"url", ""}, {"event", "e"}}), std::exception);
    EXPECT_THROW(Webhooks::process_job({{"url", "http://0.0.0.0:8080/"}, {"event", "e"}}), std::exception);
    EXPECT_THROW(Webhooks::process_job({{"url", "http://localhost./"}, {"event", "e"}}), std::exception);
}

}  // namespace
