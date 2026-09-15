#define KURLYK_AUTO_INIT 0
#include <kurlyk.hpp>
#include <server_http.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

std::string make_head_response(long status, const std::string& reason) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
        << "Content-Length: 4096\r\n"
        << "Content-Type: application/octet-stream\r\n"
        << "Connection: close\r\n\r\n";
    return out.str();
}

} // namespace

int main() {
    kurlyk::ProxyChecker checker;

    kurlyk::ProxyCheckResult invalid = checker.check(kurlyk::ProxyConfig()).get();
    require(!invalid.reachable, "invalid proxy unexpectedly reported reachable");
    require(invalid.error_code ==
                kurlyk::utils::make_error_code(kurlyk::utils::ClientError::InvalidConfiguration),
            "invalid proxy returned the wrong error code");

    HttpServer proxy_server;
    proxy_server.config.address = "127.0.0.1";
    proxy_server.config.port = 0;
    proxy_server.config.thread_pool_size = 1;

    std::promise<std::string> request_target_promise;
    std::future<std::string> request_target = request_target_promise.get_future();
    proxy_server.default_resource["HEAD"] = [&request_target_promise](
            std::shared_ptr<HttpServer::Response> response,
            std::shared_ptr<HttpServer::Request> request) {
        try {
            request_target_promise.set_value(request->path);
        } catch (...) {
        }
        *response << make_head_response(200, "OK");
    };

    std::promise<unsigned short> port_promise;
    std::future<unsigned short> port_future = port_promise.get_future();
    std::thread server_thread([&proxy_server, &port_promise]() {
        proxy_server.start([&port_promise](unsigned short port) {
            try {
                port_promise.set_value(port);
            } catch (...) {
            }
        });
    });

    const unsigned short port = port_future.get();

    kurlyk::init(true);
    kurlyk::ProxyConfig proxy;
    proxy.set_proxy("127.0.0.1", static_cast<int>(port), kurlyk::ProxyType::PROXY_HTTP);
    proxy.use = true;

    kurlyk::ProxyCheckOptions options;
    options.test_url = "http://proxy-check.invalid/probe";
    options.connect_timeout = std::chrono::milliseconds(2000);
    options.request_timeout = std::chrono::milliseconds(3000);
    options.proxy_tunnel = false;

    const kurlyk::ProxyCheckResult result = checker.check(proxy, options).get();
    require(request_target.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
            "local proxy did not receive the proxy check request");
    const std::string observed_target = request_target.get();

    require(observed_target == options.test_url,
            "proxy did not receive the absolute HTTP test URL");
    require(result.reachable, "local HTTP proxy was not reported reachable");
    require(result.http_ok, "local HTTP proxy check did not report HTTP success");
    require(!result.https_ok, "plain HTTP check unexpectedly reported HTTPS success");
    require(!result.error_code, "local HTTP proxy check returned an error");
    require(result.http_status == 200, "local HTTP proxy returned unexpected status");
    require(result.connect_latency.count() >= 0, "connect latency is negative");
    require(result.tls_latency.count() == 0, "plain HTTP check reported TLS latency");
    require(result.ttfb.count() >= result.connect_latency.count(),
            "TTFB is earlier than the connection milestone");
    require(result.total_latency.count() >= result.ttfb.count(),
            "total latency is earlier than TTFB");

    kurlyk::deinit();
    proxy_server.stop();
    server_thread.join();

    std::cout << "Proxy checker integration test passed" << std::endl;
    return 0;
}
