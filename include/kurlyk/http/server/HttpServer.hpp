#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_HTTP_SERVER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_HTTP_SERVER_HPP_INCLUDED

#include "data.hpp"
#include "detail/BasicHttpServer.hpp"
#include "backend/SimpleWeb/SimpleWebHttpServerAdapter.hpp"

namespace kurlyk {

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
    /// \brief Synchronous owned-request handler; retained alias for source compatibility.
    typedef HttpServerHandler Handler;

    /// \brief Construct a one-shot listener; zero bounds or nonpositive timeouts throw invalid_argument.
    explicit HttpServer(const HttpServerConfig& config = HttpServerConfig()) : m_implementation(config) {}
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// \brief Request shutdown without joining a caller-owned runner; safe when destroyed inside a handler.
    ~HttpServer() = default;

    /// \brief Bind an exact method/path handler before start(); duplicate routes replace the handler.
    void set_handler(const std::string& method, const std::string& path, Handler handler) {
        m_implementation.set_handler(method, path, std::move(handler));
    }

    /// \brief Bind/listen synchronously and return the actual port; throws on configuration/bind failure.
    unsigned short start() { return m_implementation.start(); }

    /// \brief Execute I/O once until stop; return establishes handler quiescence for this runner.
    /// \details Caller owns its thread and joins it. User handlers cannot be preempted by stop.
    void run() { m_implementation.run(); }

    /// \brief Request abortive shutdown from any thread; no completion or delivery acknowledgement.
    /// \details I/O stops after the current handler returns; the runner closes sockets before returning.
    void request_stop() noexcept { m_implementation.request_stop(); }

private:
    detail::BasicHttpServer<detail::SimpleWebHttpServerAdapter> m_implementation;
};

} // namespace kurlyk
#endif
