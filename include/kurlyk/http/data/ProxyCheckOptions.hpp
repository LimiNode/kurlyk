#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_OPTIONS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_OPTIONS_HPP_INCLUDED

/// \file ProxyCheckOptions.hpp
/// \brief Defines options for an HTTP-based proxy availability check.

#include <chrono>
#include <string>

namespace kurlyk {

    /// \struct ProxyCheckOptions
    /// \brief Configures the no-body request used to check a proxy.
    struct ProxyCheckOptions {
        std::string test_url;                         ///< HTTP or HTTPS URL requested through the proxy.
        std::string ca_file;                          ///< Optional CA bundle used for HTTPS verification.
        std::chrono::milliseconds connect_timeout;   ///< Maximum time allowed to establish a connection.
        std::chrono::milliseconds request_timeout;   ///< Maximum time allowed for the complete request.
        bool follow_redirects;                       ///< Follow redirects returned by the test endpoint.
        bool proxy_tunnel;                           ///< Request an HTTP proxy tunnel when supported by the proxy type.

        /// \brief Constructs options with conservative network timeouts.
        ProxyCheckOptions()
            : test_url("https://example.com/"),
              ca_file(),
              connect_timeout(3000),
              request_timeout(5000),
              follow_redirects(false),
              proxy_tunnel(false) {}
    };

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_DATA_PROXY_CHECK_OPTIONS_HPP_INCLUDED
