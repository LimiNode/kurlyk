#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_HPP_INCLUDED

/// \file
/// \brief Optional, standalone HTTP listener; not included by kurlyk.hpp.
#include <kurlyk/detail/simple_web_server.hpp>
#include <atomic>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kurlyk {

/// \brief Finite bounds for a single-run plain HTTP listener (TLS is external).
struct HttpServerConfig {
    std::string address = "127.0.0.1"; ///< Bind address; loopback by default.
    unsigned short port = 0; ///< Zero requests an ephemeral port.
    std::size_t max_request_bytes = 1024 * 1024; ///< Total received wire bytes, including headers and chunk framing.
    std::size_t max_response_body_bytes = 1024 * 1024; ///< Maximum returned body accepted from a handler.
    std::size_t max_response_header_bytes = 16 * 1024; ///< Maximum serialized application headers.
    std::size_t max_connections = 32; ///< Admitted receiving, handling, or writing connections (plus one accept socket).
    long request_timeout_seconds = 5; ///< Positive request-header timeout.
    long content_timeout_seconds = 10; ///< Positive body-read through response-write deadline (cannot preempt a handler).
};

/// \brief Owned request snapshot; safe to retain after the handler returns.
struct HttpServerRequest {
    std::string method; ///< HTTP method, unchanged.
    std::string path; ///< Backend-parsed path, without the query string.
    std::string query; ///< Undecoded query string.
    std::vector<std::pair<std::string, std::string> > headers; ///< Original header names and values; duplicates retained.
    std::string body; ///< Decoded request body, bounded by max_request_bytes.
};

/// \brief Owned, non-streaming response; framing headers are supplied by the listener.
struct HttpServerResponse {
    unsigned int status = 200; ///< Final status in [200,599]; invalid output becomes 500.
    std::vector<std::pair<std::string, std::string> > headers; ///< Application headers; CR/LF and framing headers are forbidden.
    std::string body; ///< Owned body; oversized output becomes an empty 500 response.
};

namespace detail {
#ifdef ASIO_STANDALONE
namespace server_asio = ::asio;
#else
namespace server_asio = SimpleWeb::asio;
#endif

// Bound actual received bytes independently of declared lengths and chunk arithmetic.
class HttpServerSocket : public server_asio::ip::tcp::socket {
public:
    HttpServerSocket(SimpleWeb::io_context& io, std::size_t limit)
        : server_asio::ip::tcp::socket(io), m_io(io), m_remaining(limit) {}

    template<class Buffers, class Handler>
    void async_read_some(const Buffers& buffers, Handler handler) {
        if (m_remaining == 0) {
            SimpleWeb::post(m_io, [handler]() mutable {
                handler(SimpleWeb::make_error_code::make_error_code(SimpleWeb::errc::message_size), 0);
            });
            return;
        }
        auto it = server_asio::buffer_sequence_begin(buffers);
        const auto end = server_asio::buffer_sequence_end(buffers);
        while (it != end && server_asio::buffer_size(*it) == 0) ++it;
        const server_asio::mutable_buffer buffer = it == end
            ? server_asio::mutable_buffer() : server_asio::buffer(*it, m_remaining);
        server_asio::ip::tcp::socket::async_read_some(buffer,
            [this, handler](const SimpleWeb::error_code& error, std::size_t count) mutable {
                m_remaining -= count;
                handler(error, count);
            });
    }
private:
    SimpleWeb::io_context& m_io;
    std::size_t m_remaining;
};

class HttpServerBackend : public SimpleWeb::ServerBase<HttpServerSocket> {
public:
    explicit HttpServerBackend(const HttpServerConfig& config)
        : SimpleWeb::ServerBase<HttpServerSocket>(config.port),
          m_limit(config.max_connections), m_wire_limit(config.max_request_bytes), m_port(0) {}
    unsigned short port() const { return m_port; }
protected:
    void after_bind() override { m_port = acceptor->local_endpoint().port(); }
    void accept() override {
        auto connection = create_connection(*io_service, m_wire_limit);
        acceptor->async_accept(*connection->socket, [this, connection](const SimpleWeb::error_code& error) {
            auto runner = connection->handler_runner->continue_lock();
            if (!runner) return;
            if (!error) {
                bool admitted;
                {
                    SimpleWeb::LockGuard guard(connections->mutex);
                    admitted = connections->set.size() <= m_limit;
                }
                if (admitted) {
                    auto session = std::make_shared<Session>(config.max_request_streambuf_size, connection);
                    read(session);
                } else {
                    // No HTTP work or request buffer is allocated for overload.
                    SimpleWeb::error_code ignored;
                    connection->socket->close(ignored);
                }
            }
            if (error != SimpleWeb::error::operation_aborted && acceptor->is_open()) accept();
        });
    }
private:
    std::size_t m_limit;
    std::size_t m_wire_limit;
    unsigned short m_port;
};
} // namespace detail

/// \brief Bounded HTTP/1.x listener with exact method/path routing and no owned worker.
///
/// Configure routes, call start(), then run() once on a caller-owned thread.
/// Handlers execute serially on that thread and must return promptly. Each
/// connection handles one request and is closed after its response. Overload,
/// malformed requests, receive limits, disconnects, and timeouts may close the
/// connection without an HTTP response. No retry or external-delivery guarantee
/// is provided. request_stop() is safe inside a handler; run() must return before
/// the caller treats shutdown as quiescent. Starting/configuring/running require
/// owner serialization; only request_stop() may race with run().
class HttpServer {
public:
    /// \brief Synchronous handler returning an owned response; exceptions become empty 500 responses.
    typedef std::function<HttpServerResponse(HttpServerRequest)> Handler;

    /// \brief Construct a one-shot listener; zero bounds or nonpositive timeouts throw invalid_argument.
    explicit HttpServer(const HttpServerConfig& config = HttpServerConfig())
        : m_state(std::make_shared<State>(config)) {}
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// \brief Request shutdown without joining a caller-owned runner; safe when destroyed inside a handler.
    ~HttpServer() { request_stop(); }

    /// \brief Bind an exact method/path handler before start(); duplicate routes replace the handler.
    void set_handler(const std::string& method, const std::string& path, Handler handler) {
        auto state = m_state;
        if (state->m_started) throw std::logic_error("HTTP server already started");
        if (method.empty() || path.empty() || path[0] != '/' || !handler)
            throw std::invalid_argument("Invalid HTTP server route");
        state->m_routes[std::make_pair(method, path)] = std::move(handler);
    }

    /// \brief Bind/listen synchronously and return the actual port; throws on configuration/bind failure.
    unsigned short start() {
        auto state = m_state;
        if (state->m_started || state->m_stopping.load()) throw std::logic_error("HTTP server is one-shot");
        state->m_started = true;
        const std::weak_ptr<State> weak(state);
        const char* methods[] = {"GET", "POST", "HEAD", "PUT", "PATCH", "DELETE", "OPTIONS", "TRACE", "CONNECT"};
        for (const char* method : methods) state->install(method, weak);
        for (const auto& route : state->m_routes) state->install(route.first.first, weak);
        state->m_backend.start();
        return state->m_backend.port();
    }

    /// \brief Execute I/O once until stop; return establishes handler quiescence for this runner.
    /// \details Caller owns its thread and joins it. User handlers cannot be preempted by stop.
    void run() {
        auto state = m_state; // State, backend, and callbacks outlive facade destruction.
        if (!state->m_started || state->m_run_called.exchange(true))
            throw std::logic_error("HTTP server requires start and exactly one runner");
        try { state->m_io->run(); }
        catch (...) { state->m_backend.stop(); throw; }
        state->m_backend.stop();
        // Drain aborted network operations to release their buffers and sockets.
        SimpleWeb::restart(*state->m_io);
        state->m_io->run();
    }

    /// \brief Request abortive shutdown from any thread; no completion or delivery acknowledgement.
    /// \details I/O stops after the current handler returns; the runner closes sockets before returning.
    void request_stop() noexcept {
        auto state = m_state;
        state->m_stopping.store(true);
        state->m_io->stop();
    }

private:
    struct State {
        HttpServerConfig m_config;
        std::shared_ptr<SimpleWeb::io_context> m_io;
        detail::HttpServerBackend m_backend;
        std::map<std::pair<std::string, std::string>, Handler> m_routes;
        bool m_started = false;
        std::atomic<bool> m_run_called{false};
        std::atomic<bool> m_stopping{false};

        explicit State(const HttpServerConfig& config)
            : m_config(config), m_io(std::make_shared<SimpleWeb::io_context>()), m_backend(config) {
            if (config.max_request_bytes == 0 || config.max_connections == 0 ||
                config.max_response_body_bytes == 0 || config.max_response_header_bytes == 0 ||
                config.request_timeout_seconds <= 0 || config.content_timeout_seconds <= 0)
                throw std::invalid_argument("HTTP server requires finite positive bounds");
            m_backend.io_service = m_io;
            m_backend.config.address = config.address;
            m_backend.config.max_request_streambuf_size = config.max_request_bytes;
            m_backend.config.timeout_request = config.request_timeout_seconds;
            m_backend.config.timeout_content = config.content_timeout_seconds;
            m_backend.on_error = [](std::shared_ptr<detail::HttpServerBackend::Request>, const SimpleWeb::error_code&) {};
        }
        void install(const std::string& method, const std::weak_ptr<State>& weak) {
            m_backend.default_resource[method] = [weak](std::shared_ptr<detail::HttpServerBackend::Response> output,
                                                       std::shared_ptr<detail::HttpServerBackend::Request> input) {
                output->close_connection_after_response = true;
                auto state = weak.lock();
                if (!state || state->m_stopping.load()) return;
                HttpServerResponse response;
                response.status = 404;
                try {
                    bool valid_framing = input->header.count("Content-Length") <= 1 && input->header.count("Transfer-Encoding") <= 1;
                    const auto length = input->header.find("Content-Length");
                    const auto encoding = input->header.find("Transfer-Encoding");
                    if (length != input->header.end()) {
                        valid_framing = valid_framing && !length->second.empty() &&
                            length->second.find_first_not_of("0123456789") == std::string::npos && encoding == input->header.end();
                    }
                    if (encoding != input->header.end()) valid_framing = valid_framing && encoding->second == "chunked";
                    auto it = state->m_routes.find(std::make_pair(input->method, input->path));
                    if (!valid_framing) response.status = 400;
                    else if (it != state->m_routes.end()) {
                        HttpServerRequest request;
                        request.method = input->method;
                        request.path = input->path;
                        request.query = input->query_string;
                        request.headers.assign(input->header.begin(), input->header.end());
                        request.body.assign(std::istreambuf_iterator<char>(input->content), std::istreambuf_iterator<char>());
                        response = it->second(std::move(request));
                    }
                } catch (...) {
                    response = HttpServerResponse();
                    response.status = 500;
                }
                state->write_response(output, input->method, response);
            };
        }
        void write_response(const std::shared_ptr<detail::HttpServerBackend::Response>& output,
                            const std::string& method, HttpServerResponse& response) {
            std::size_t bytes = 0;
            bool valid = response.status >= 200 && response.status <= 599 &&
                response.body.size() <= m_config.max_response_body_bytes;
            for (const auto& header : response.headers) {
                const auto& name = header.first;
                const auto& value = header.second;
                const std::size_t remaining = m_config.max_response_header_bytes - bytes;
                if (name.empty() || name.find_first_not_of("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos ||
                    value.find_first_of("\r\n") != std::string::npos || name.size() > remaining ||
                    value.size() > remaining - name.size() || remaining - name.size() - value.size() < 4 ||
                    SimpleWeb::case_insensitive_equal(name, "Content-Length") ||
                    SimpleWeb::case_insensitive_equal(name, "Transfer-Encoding") ||
                    SimpleWeb::case_insensitive_equal(name, "Connection")) { valid = false; break; }
                bytes += name.size() + value.size() + 4;
            }
            if (!valid) { response = HttpServerResponse(); response.status = 500; }
            *output << "HTTP/1.1 " << response.status << " Response\r\nConnection: close\r\n";
            for (const auto& header : response.headers) *output << header.first << ": " << header.second << "\r\n";
            const bool no_body = response.status == 204 || response.status == 304;
            *output << "Content-Length: " << (no_body ? 0 : response.body.size()) << "\r\n\r\n";
            if (method != "HEAD" && !no_body) output->write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
        }
    };
    std::shared_ptr<State> m_state;
};
} // namespace kurlyk
#endif
