#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_OPTIONS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_OPTIONS_HPP_INCLUDED

/// \file ProxyCheckOptions.hpp
/// \brief Defines options for an HTTP-based proxy availability check.

#include <string>

namespace kurlyk {

    /// \struct ProxyCheckOptions
    /// \brief Configures the no-body request used to check a proxy.
    struct ProxyCheckOptions {
        std::string test_url = "https://example.com/"; ///< HTTP or HTTPS URL requested through the proxy.
        std::string ca_file;                            ///< Optional CA bundle used for HTTPS verification.
        long connect_timeout = 3;                      ///< Connection timeout in seconds.
        long request_timeout = 5;                      ///< Complete request timeout in seconds.
        bool follow_redirects = false;                 ///< Follow redirects returned by the test endpoint.
        bool proxy_tunnel = false;                     ///< Force an HTTP CONNECT tunnel where supported.
    };

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_OPTIONS_HPP_INCLUDED
