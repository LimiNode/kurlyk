#include <kurlyk/http_server.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed line " << __LINE__ << ": " #condition << std::endl; std::abort(); } } while (false)

namespace {
namespace net = kurlyk::detail::server_asio;
typedef net::ip::tcp::socket Socket;

Socket connect(SimpleWeb::io_context& io, unsigned short port) {
    Socket socket(io);
    socket.connect(net::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port));
    socket.non_blocking(true);
    return socket;
}
void send(Socket& socket, const std::string& bytes) {
    SimpleWeb::error_code error;
    socket.non_blocking(false);
    net::write(socket, net::buffer(bytes), error);
    socket.non_blocking(true);
}
std::string read_closed(Socket& socket) {
    std::string result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        char buffer[1024];
        SimpleWeb::error_code error;
        const auto count = socket.read_some(net::buffer(buffer), error);
        result.append(buffer, count);
        if (error == net::error::eof || error == net::error::connection_reset || error == net::error::connection_aborted) return result;
        CHECK(!error || error == net::error::would_block || error == net::error::try_again);
        if (error) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(false && "socket was not closed");
    return result;
}
std::string round_trip(unsigned short port, const std::string& request) {
    SimpleWeb::io_context io;
    Socket socket = connect(io, port);
    send(socket, request);
    return read_closed(socket);
}
kurlyk::HttpServerResponse echo(kurlyk::HttpServerRequest request) {
    kurlyk::HttpServerResponse response;
    response.body = request.body;
    return response;
}
void request_bounds() {
    kurlyk::HttpServerConfig config;
    config.max_request_bytes = 256;
    config.max_response_body_bytes = 64;
    config.request_timeout_seconds = 1;
    config.content_timeout_seconds = 1;
    kurlyk::HttpServer server(config);
    std::atomic<int> calls{0};
    std::promise<kurlyk::HttpServerRequest> retained;
    server.set_handler("POST", "/owned", [&retained](kurlyk::HttpServerRequest request) {
        retained.set_value(std::move(request));
        return kurlyk::HttpServerResponse();
    });
    server.set_handler("POST", "/echo", [&calls](kurlyk::HttpServerRequest request) {
        ++calls;
        return echo(std::move(request));
    });
    server.set_handler("GET", "/large", [](kurlyk::HttpServerRequest) {
        kurlyk::HttpServerResponse result;
        result.body.assign(65, 'x');
        return result;
    });
    server.set_handler("GET", "/bad-header", [](kurlyk::HttpServerRequest) {
        kurlyk::HttpServerResponse result;
        result.headers.push_back(std::make_pair("X-Test", "ok\r\nInjected: true"));
        return result;
    });
    const unsigned short port = server.start();
    std::thread runner([&server] { server.run(); });
    CHECK(round_trip(port, "POST /owned?q=1 HTTP/1.1\r\nX-Owned: value\r\nContent-Length: 3\r\n\r\nabc").find("200") != std::string::npos);
    const auto snapshot = retained.get_future().get();
    CHECK(snapshot.body == "abc" && snapshot.path == "/owned" && snapshot.query == "q=1" && snapshot.headers.size() == 2);
    CHECK(round_trip(port, "POST /echo?q=1 HTTP/1.1\r\nHost: local\r\nContent-Length: 3\r\n\r\nabc").find("\r\n\r\nabc") != std::string::npos);
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n").find("\r\n\r\nabc") != std::string::npos);
    CHECK(calls == 2);
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n").find("400") != std::string::npos);
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nContent-Length: 0junk\r\n\r\n").find("400") != std::string::npos);
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nContent-Length: 300\r\n\r\n").find("200") == std::string::npos);
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nX-Large: " + std::string(300, 'x') + "\r\n\r\n").find("200") == std::string::npos);
    // Fragmented chunked body exceeds the cumulative wire budget without a declared body size.
    SimpleWeb::io_context io;
    Socket chunked = connect(io, port);
    send(chunked, "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    for (int i = 0; i != 5; ++i) {
        send(chunked, "32\r\n" + std::string(50, 'x') + "\r\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(read_closed(chunked).find("200") == std::string::npos);
    // Malicious overflowing chunk length cannot bypass the actual received-byte cap.
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\nffffffffffffffff\r\n" + std::string(220, 'x')).find("200") == std::string::npos);
    CHECK(calls == 2);
    CHECK(round_trip(port, "GET /large HTTP/1.1\r\n\r\n").find("500") != std::string::npos);
    CHECK(round_trip(port, "GET /bad-header HTTP/1.1\r\n\r\n").find("Injected") == std::string::npos);
    CHECK(round_trip(port, "GET /missing HTTP/1.1\r\n\r\n").find("404") != std::string::npos);
    // Disconnect while receiving: listener remains usable.
    { Socket dropped = connect(io, port); send(dropped, "POST /echo HTTP/1.1\r\nContent-Length: 8\r\n\r\na"); }
    CHECK(round_trip(port, "POST /echo HTTP/1.1\r\nContent-Length: 0\r\n\r\n").find("200") != std::string::npos);
    Socket timed = connect(io, port);
    send(timed, "GET /");
    CHECK(read_closed(timed).empty());
    server.request_stop();
    runner.join();
}
void overload_and_stop() {
    kurlyk::HttpServerConfig config;
    config.max_connections = 1;
    kurlyk::HttpServer server(config);
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    server.set_handler("GET", "/hold", [&mutex, &cv, &entered, &release](kurlyk::HttpServerRequest) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&release] { return release; });
        return kurlyk::HttpServerResponse();
    });
    unsigned short port = server.start();
    std::thread runner([&server] { server.run(); });
    SimpleWeb::io_context io;
    Socket idle = connect(io, port);
    send(idle, "GET /");
    // Give the loopback runner time to admit the first incomplete request.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Socket excess = connect(io, port);
    CHECK(read_closed(excess).empty());
    idle.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Socket holding = connect(io, port);
    send(holding, "GET /hold HTTP/1.1\r\n\r\n");
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(cv.wait_for(lock, std::chrono::seconds(3), [&entered] { return entered; }));
    }
    server.request_stop(); // Must not wait for the executing user handler.
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();
    runner.join();
    CHECK(read_closed(holding).empty()); // Abortive stop never promises delivery.
}
void reentrant_destruction() {
    for (int iteration = 0; iteration != 3; ++iteration) {
        std::unique_ptr<kurlyk::HttpServer> server(new kurlyk::HttpServer());
        server->set_handler("GET", "/stop", [&server](kurlyk::HttpServerRequest) {
            server.reset(); // No self-join; run() owns internal state until return.
            return kurlyk::HttpServerResponse();
        });
        const auto port = server->start();
        kurlyk::HttpServer* raw = server.get();
        std::thread runner([raw] { raw->run(); });
        CHECK(round_trip(port, "GET /stop HTTP/1.1\r\n\r\n").empty());
        runner.join();
        CHECK(!server);
    }
}
void lifecycle_validation() {
    kurlyk::HttpServerConfig config;
    config.max_connections = 0;
    bool rejected = false;
    try { kurlyk::HttpServer invalid(config); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    kurlyk::HttpServer server;
    server.start();
    rejected = false;
    try { server.start(); } catch (const std::logic_error&) { rejected = true; }
    CHECK(rejected);
    server.request_stop();
    server.request_stop();
    server.run(); // Stop before the runner starts does not hang.
}
void write_timeout() {
    kurlyk::HttpServerConfig config;
    const std::size_t body_size = 16 * 1024 * 1024;
    config.max_response_body_bytes = body_size;
    config.content_timeout_seconds = 1;
    kurlyk::HttpServer server(config);
    server.set_handler("GET", "/large", [body_size](kurlyk::HttpServerRequest) {
        kurlyk::HttpServerResponse response;
        response.body.assign(body_size, 'x');
        return response;
    });
    const auto port = server.start();
    std::thread runner([&server] { server.run(); });
    SimpleWeb::io_context io;
    Socket slow(io);
    slow.open(net::ip::tcp::v4());
    slow.set_option(net::socket_base::receive_buffer_size(1024));
    slow.connect(net::ip::tcp::endpoint(SimpleWeb::make_address("127.0.0.1"), port));
    send(slow, "GET /large HTTP/1.1\r\n\r\n");
    // No reads while the server's finite write deadline expires.
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto response = read_closed(slow);
    CHECK(response.size() < body_size);
    server.request_stop();
    runner.join();
}
}
int main() {
    request_bounds();
    overload_and_stop();
    reentrant_destruction();
    lifecycle_validation();
    write_timeout();
    std::cout << "HTTP server foundation checks passed\n";
}
