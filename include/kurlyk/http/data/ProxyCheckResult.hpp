#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_RESULT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_RESULT_HPP_INCLUDED

/// \file ProxyCheckResult.hpp
/// \brief Defines the result of an HTTP-based proxy availability check.

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

        long connect_latency_ms = 0; ///< Elapsed time until the TCP connection was established, in milliseconds.
        long tls_latency_ms = 0;     ///< TLS setup time after TCP connect, in milliseconds; includes proxy CONNECT negotiation.
        long ttfb_ms = 0;            ///< Elapsed time until the first response byte, in milliseconds.
        long total_latency_ms = 0;   ///< Total request time, in milliseconds.
    };

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_RESULT_HPP_INCLUDED
