#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_RESULT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_RESULT_HPP_INCLUDED

/// \file ProxyCheckResult.hpp
/// \brief Defines the result of an HTTP-based proxy availability check.

#include <chrono>
#include <string>
#include <system_error>

namespace kurlyk {

    /// \struct ProxyCheckResult
    /// \brief Stores proxy reachability, protocol status, errors, and curl timing metrics.
    struct ProxyCheckResult {
        bool reachable = false; ///< True when an HTTP response was received through the proxy.
        bool http_ok = false;   ///< True when the test endpoint returned a 2xx or 3xx response.
        bool https_ok = false;  ///< True when an HTTPS test returned a 2xx or 3xx response.

        std::error_code error_code; ///< Transport, HTTP, or client-side error.
        std::string error_message;  ///< Error details supplied by the HTTP backend.
        long http_status = 0;       ///< HTTP status returned by the test endpoint or proxy.

        std::chrono::milliseconds connect_latency{0}; ///< Elapsed time until the TCP connection was established.
        std::chrono::milliseconds tls_latency{0};     ///< TLS setup time after TCP connect; includes proxy CONNECT negotiation.
        std::chrono::milliseconds ttfb{0};            ///< Elapsed time until the first response byte.
        std::chrono::milliseconds total_latency{0};   ///< Total request time.
    };

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_RESULT_HPP_INCLUDED
