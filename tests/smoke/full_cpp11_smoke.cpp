#define KURLYK_AUTO_INIT 0
#define KURLYK_HTTP_SUPPORT 1
#define KURLYK_WEBSOCKET_SUPPORT 1

#include <kurlyk.hpp>

#if defined(KURLYK_EXPECT_STANDALONE)
#   if KURLYK_USE_BOOST_ASIO
#       error "CMake target selected Boost.Asio in standalone mode"
#   endif
#   ifndef ASIO_STANDALONE
#       error "CMake target did not select ASIO_STANDALONE"
#   endif
#else
#   if !KURLYK_USE_BOOST_ASIO
#       error "CMake target did not select Boost.Asio"
#   endif
#   if defined(ASIO_STANDALONE)
#       error "CMake target selected ASIO_STANDALONE in Boost mode"
#   endif
#endif

#include <memory>

int main() {
    kurlyk::HttpClient http_client("https://example.com");
    http_client.set_max_in_flight(1);

    kurlyk::WebSocketClient websocket_client("ws://example.com");
    websocket_client.on_event([](std::unique_ptr<kurlyk::WebSocketEventData>) {});

    kurlyk::OAuthConfig oauth_config;
    (void)oauth_config;

    kurlyk::ProxyCheckOptions proxy_check_options;
    kurlyk::ProxyCheckResult proxy_check_result;
    kurlyk::ProxyChecker proxy_checker;
    std::future<kurlyk::ProxyCheckResult> proxy_check_future =
        proxy_checker.check(kurlyk::ProxyConfig());
    (void)proxy_check_options;
    (void)proxy_check_result;
    (void)proxy_checker;
    (void)proxy_check_future;

    return 0;
}
