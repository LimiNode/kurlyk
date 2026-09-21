#include <kurlyk.hpp>

#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: proxy_checker_example <host:port> [test-url]" << std::endl;
        return 0;
    }

    kurlyk::ProxyConfig proxy;
    proxy.proxy_server = argv[1];
    proxy.proxy_type = kurlyk::ProxyType::PROXY_HTTP;
    proxy.use = true;

    kurlyk::ProxyCheckOptions options;
    if (argc >= 3) options.test_url = argv[2];

    const kurlyk::ProxyCheckResult result = kurlyk::check_proxy(proxy, options).get();

    std::cout
        << "reachable: " << std::boolalpha << result.reachable << '\n'
        << "http_ok: " << result.http_ok << '\n'
        << "https_ok: " << result.https_ok << '\n'
        << "status: " << result.http_status << '\n'
        << "connect: " << result.connect_latency_ms << " ms\n"
        << "tls/connect: " << result.tls_latency_ms << " ms\n"
        << "ttfb: " << result.ttfb_ms << " ms\n"
        << "total: " << result.total_latency_ms << " ms\n";

    if (result.error_code) {
        std::cerr << "error: " << result.error_code.message();
        if (!result.error_message.empty()) {
            std::cerr << " (" << result.error_message << ')';
        }
        std::cerr << std::endl;
        return 1;
    }
    return result.http_ok ? 0 : 2;
}
