#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_DATA_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_DATA_HPP_INCLUDED

/// \file
/// \brief Standalone HTTP server contracts without backend or client dependencies.
#include <cstddef>
#include <functional>
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
    std::string body; ///< Decoded body bytes, bounded by max_request_bytes; no JSON conversion is performed.
};

/// \brief Owned, non-streaming response; framing headers are supplied by the listener.
struct HttpServerResponse {
    unsigned int status = 200; ///< Final status in [200,599]; invalid output becomes 500.
    std::vector<std::pair<std::string, std::string> > headers; ///< Application headers; framing headers, CTLs except HTAB, and DEL are forbidden.
    std::string body; ///< Owned body; suppressed for HEAD and status 204/205/304; oversized output becomes an empty 500.
};

/// \brief Synchronous handler taking an owned request and returning an owned response.
/// \details Listener invokes handlers serially on its caller-owned runner; exceptions become empty 500 responses.
typedef std::function<HttpServerResponse(HttpServerRequest)> HttpServerHandler;

} // namespace kurlyk
#endif
