#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_HTTP_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_HTTP_UTILS_HPP_INCLUDED

/// \file http_utils.hpp
/// \brief Provides utility functions for working with HTTP headers, cookies, and query strings.

namespace kurlyk {
namespace utils {

    /// \brief Removes the first occurrence of "https://" or "http://" from the given URL.
    /// \param url The URL from which to remove the substring.
    /// \return std::string The modified URL with the first occurrence of "https://" or "http://" removed.
    inline std::string remove_http_prefix(const std::string& url) {
        const std::string https_prefix = "https://";
        const std::string http_prefix = "http://";

        std::string modified_url = url;

        // Find the position of "https://" or "http://"
        std::size_t pos = modified_url.find(https_prefix);
        if (pos == std::string::npos) {
            pos = modified_url.find(http_prefix);
        }

        // If found, erase the substring
        if (pos != std::string::npos) {
            if (modified_url.compare(pos, https_prefix.length(), https_prefix) == 0) {
                modified_url.erase(pos, https_prefix.length());
            } else if (modified_url.compare(pos, http_prefix.length(), http_prefix) == 0) {
                modified_url.erase(pos, http_prefix.length());
            }
        }

        return modified_url;
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_HTTP_UTILS_HPP_INCLUDED
