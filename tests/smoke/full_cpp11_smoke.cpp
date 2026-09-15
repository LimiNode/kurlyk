#define KURLYK_AUTO_INIT 0
#define KURLYK_HTTP_SUPPORT 1
#define KURLYK_WEBSOCKET_SUPPORT 1

#include <kurlyk.hpp>

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
