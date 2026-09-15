#define KURLYK_AUTO_INIT 0
#include <kurlyk.hpp>
#include <server_https.hpp>
#include <server_http.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using HttpsServer = SimpleWeb::Server<SimpleWeb::HTTPS>;
#ifdef ASIO_STANDALONE
namespace test_asio = asio;
#else
namespace test_asio = SimpleWeb::asio;
#endif
using Tcp = test_asio::ip::tcp;
using ErrorCode = SimpleWeb::error_code;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

std::string make_head_response(long status, const std::string& reason) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
        << "Content-Length: 0\r\n"
        << "Connection: close\r\n\r\n";
    return out.str();
}

class ConnectProxy {
public:
    explicit ConnectProxy(unsigned short origin_port)
        : acceptor_(io_context_, Tcp::endpoint(
              SimpleWeb::make_address("127.0.0.1"), 0)),
          origin_endpoint_(
              SimpleWeb::make_address("127.0.0.1"), origin_port) {}

    ~ConnectProxy() {
        stop();
    }

    unsigned short port() const {
        return acceptor_.local_endpoint().port();
    }

    std::future<std::string> target_future() {
        return target_promise_.get_future();
    }

    void start() {
        thread_ = std::thread([this]() { run(); });
    }

    void stop() {
        ErrorCode error;
        acceptor_.close(error);
        close_active_sockets();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    test_asio::io_context io_context_;
    Tcp::acceptor acceptor_;
    Tcp::endpoint origin_endpoint_;
    std::thread thread_;
    std::promise<std::string> target_promise_;
    std::mutex sockets_mutex_;
    std::shared_ptr<Tcp::socket> client_socket_;
    std::shared_ptr<Tcp::socket> origin_socket_;

    void set_target(const std::string& target) {
        try {
            target_promise_.set_value(target);
        } catch (const std::future_error&) {
        }
    }

    void close_active_sockets() {
        std::lock_guard<std::mutex> lock(sockets_mutex_);
        close_socket(client_socket_);
        close_socket(origin_socket_);
    }

    static void close_socket(const std::shared_ptr<Tcp::socket>& socket) {
        if (!socket) return;
        ErrorCode error;
        socket->shutdown(Tcp::socket::shutdown_both, error);
        socket->close(error);
    }

    static void copy_stream(
            const std::shared_ptr<Tcp::socket>& source,
            const std::shared_ptr<Tcp::socket>& destination) {
        std::array<char, 8192> buffer;
        ErrorCode read_error;
        while (!read_error) {
            const std::size_t count = source->read_some(
                test_asio::buffer(buffer), read_error);
            if (read_error || count == 0) break;

            ErrorCode write_error;
            test_asio::write(
                *destination,
                test_asio::buffer(buffer.data(), count),
                write_error);
            if (write_error) break;
        }

        ErrorCode shutdown_error;
        destination->shutdown(Tcp::socket::shutdown_send, shutdown_error);
    }

    void run() {
        try {
            const std::shared_ptr<Tcp::socket> client(new Tcp::socket(io_context_));
            acceptor_.accept(*client);

            test_asio::streambuf request_buffer;
            test_asio::read_until(*client, request_buffer, "\r\n\r\n");
            std::istream request_stream(&request_buffer);
            std::string request_line;
            std::getline(request_stream, request_line);
            if (!request_line.empty() && request_line[request_line.size() - 1] == '\r') {
                request_line.erase(request_line.size() - 1);
            }

            std::istringstream request_line_stream(request_line);
            std::string method;
            std::string target;
            std::string version;
            request_line_stream >> method >> target >> version;
            if (method != "CONNECT" || target.empty() || version.empty()) {
                throw std::runtime_error("invalid CONNECT request");
            }
            set_target(target);

            const std::shared_ptr<Tcp::socket> origin(new Tcp::socket(io_context_));
            origin->connect(origin_endpoint_);
            {
                std::lock_guard<std::mutex> lock(sockets_mutex_);
                client_socket_ = client;
                origin_socket_ = origin;
            }

            const std::string response =
                "HTTP/1.1 200 Connection Established\r\n"
                "Proxy-Agent: kurlyk-test\r\n"
                "\r\n";
            test_asio::write(*client, test_asio::buffer(response));

            std::thread client_to_origin([client, origin]() {
                copy_stream(client, origin);
            });
            std::thread origin_to_client([client, origin]() {
                copy_stream(origin, client);
            });
            client_to_origin.join();
            origin_to_client.join();
        } catch (const std::exception& error) {
            set_target(std::string());
            std::cerr << "CONNECT proxy error: " << error.what() << std::endl;
        }

        close_active_sockets();
    }
};

} // namespace

int main() {
    kurlyk::ProxyChecker checker;

    const kurlyk::ProxyCheckOptions default_options;
    require(!default_options.follow_redirects,
            "proxy checker follows redirects by default");
    require(!default_options.proxy_tunnel,
            "proxy checker enables proxy tunneling by default");

    kurlyk::ProxyCheckResult invalid = checker.check(kurlyk::ProxyConfig()).get();
    require(!invalid.reachable, "invalid proxy unexpectedly reported reachable");
    require(invalid.error_code ==
                kurlyk::utils::make_error_code(kurlyk::utils::ClientError::InvalidConfiguration),
            "invalid proxy returned the wrong error code");

    HttpsServer https_server(KURLYK_TEST_SSL_CERT_FILE, KURLYK_TEST_SSL_KEY_FILE);
    https_server.config.address = "127.0.0.1";
    https_server.config.port = 0;
    https_server.config.thread_pool_size = 1;

    std::promise<std::string> https_request_promise;
    std::future<std::string> https_request = https_request_promise.get_future();
    https_server.default_resource["HEAD"] = [&https_request_promise](
            std::shared_ptr<HttpsServer::Response> response,
            std::shared_ptr<HttpsServer::Request> request) {
        try {
            https_request_promise.set_value(request->method + " " + request->path);
        } catch (...) {
        }
        *response << make_head_response(200, "OK");
    };

    std::promise<unsigned short> https_port_promise;
    std::future<unsigned short> https_port_future = https_port_promise.get_future();
    std::thread https_server_thread([&https_server, &https_port_promise]() {
        https_server.start([&https_port_promise](unsigned short port) {
            try {
                https_port_promise.set_value(port);
            } catch (...) {
            }
        });
    });
    const unsigned short https_port = https_port_future.get();

    HttpServer http_proxy_server;
    http_proxy_server.config.address = "127.0.0.1";
    http_proxy_server.config.port = 0;
    http_proxy_server.config.thread_pool_size = 1;

    std::promise<std::string> http_target_promise;
    std::future<std::string> http_target = http_target_promise.get_future();
    http_proxy_server.default_resource["HEAD"] = [&http_target_promise](
            std::shared_ptr<HttpServer::Response> response,
            std::shared_ptr<HttpServer::Request> request) {
        try {
            http_target_promise.set_value(request->path);
        } catch (...) {
        }
        *response << make_head_response(200, "OK");
    };

    std::promise<unsigned short> http_proxy_port_promise;
    std::future<unsigned short> http_proxy_port_future = http_proxy_port_promise.get_future();
    std::thread http_proxy_thread([&http_proxy_server, &http_proxy_port_promise]() {
        http_proxy_server.start([&http_proxy_port_promise](unsigned short port) {
            try {
                http_proxy_port_promise.set_value(port);
            } catch (...) {
            }
        });
    });
    const unsigned short http_proxy_port = http_proxy_port_future.get();

    kurlyk::init(true);
    kurlyk::ProxyConfig proxy;
    proxy.set_proxy("127.0.0.1", static_cast<int>(http_proxy_port), kurlyk::ProxyType::PROXY_HTTP);
    proxy.use = true;

    kurlyk::ProxyCheckOptions http_options;
    http_options.test_url = "http://proxy-check.invalid/probe";
    http_options.connect_timeout = std::chrono::milliseconds(2000);
    http_options.request_timeout = std::chrono::milliseconds(3000);

    const kurlyk::ProxyCheckResult http_result = checker.check(proxy, http_options).get();
    require(http_target.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
            "local proxy did not receive the proxy check request");
    require(http_target.get() == http_options.test_url,
            "proxy did not receive the absolute HTTP test URL");
    require(http_result.reachable, "local HTTP proxy was not reported reachable");
    require(http_result.http_ok, "local HTTP proxy check did not report HTTP success");
    require(!http_result.https_ok, "plain HTTP check unexpectedly reported HTTPS success");
    require(!http_result.error_code, "local HTTP proxy check returned an error");
    require(http_result.http_status == 200, "local HTTP proxy returned unexpected status");
    require(http_result.connect_latency.count() >= 0, "connect latency is negative");
    require(http_result.tls_latency.count() == 0, "plain HTTP check reported TLS latency");
    require(http_result.ttfb.count() >= http_result.connect_latency.count(),
            "TTFB is earlier than the connection milestone");
    require(http_result.total_latency.count() >= http_result.ttfb.count(),
            "total latency is earlier than TTFB");

    http_proxy_server.stop();
    http_proxy_thread.join();

    ConnectProxy connect_proxy(https_port);
    std::future<std::string> connect_target = connect_proxy.target_future();
    connect_proxy.start();

    proxy.set_proxy("127.0.0.1", static_cast<int>(connect_proxy.port()), kurlyk::ProxyType::PROXY_HTTP);
    kurlyk::ProxyCheckOptions https_options;
    https_options.test_url = "https://127.0.0.1:" +
        std::to_string(static_cast<unsigned long>(https_port)) + "/probe";
    https_options.ca_file = KURLYK_TEST_SSL_CA_FILE;
    https_options.connect_timeout = std::chrono::milliseconds(2000);
    https_options.request_timeout = std::chrono::milliseconds(3000);
    https_options.proxy_tunnel = true;

    const kurlyk::ProxyCheckResult https_result = checker.check(proxy, https_options).get();
    require(connect_target.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
            "CONNECT proxy did not receive the HTTPS request");
    require(connect_target.get() ==
                "127.0.0.1:" + std::to_string(static_cast<unsigned long>(https_port)),
            "CONNECT proxy received an unexpected target");
    require(https_request.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
            "HTTPS origin did not receive the proxy check request");
    require(https_request.get() == "HEAD /probe",
            "HTTPS origin received an unexpected request");
    require(https_result.reachable, "HTTPS proxy check was not reported reachable");
    require(https_result.http_ok, "HTTPS proxy check did not report HTTP success");
    require(https_result.https_ok, "HTTPS proxy check did not report HTTPS success");
    require(!https_result.error_code, "HTTPS proxy check returned an error");
    require(https_result.http_status == 200, "HTTPS proxy returned unexpected status");
    require(https_result.tls_latency.count() > 0, "HTTPS proxy check did not report TLS latency");

    connect_proxy.stop();
    kurlyk::deinit();
    https_server.stop();
    https_server_thread.join();

    std::cout << "Proxy checker integration test passed" << std::endl;
    return 0;
}
