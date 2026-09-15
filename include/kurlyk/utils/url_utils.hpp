#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_URL_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_URL_UTILS_HPP_INCLUDED

/// \file url_utils.hpp
/// \brief Provides utility functions for parsing and validating URLs and their components.

namespace kurlyk {
namespace utils {

    /// \brief Extracts the protocol from a URL.
    /// \param url The URL string.
    /// \return The protocol scheme as a string. Returns an empty string if no protocol is found.
    inline std::string extract_protocol(const std::string& url) {
        std::string protocol;
        std::size_t pos = url.find("://");

        if (pos != std::string::npos) {
            protocol = url.substr(0, pos);
        }

        return protocol;
    }

    /// \brief Removes the first occurrence of "wss://" or "ws://" from the given URL.
    /// \param url The URL from which to remove the substring.
    /// \return std::string The modified URL with the first occurrence of "wss://" or "ws://" removed.
    inline std::string remove_ws_prefix(const std::string& url) {
        const std::string wss_prefix = "wss://";
        const std::string ws_prefix = "ws://";

        std::string modified_url = url;

        // Find the position of "wss://" or "ws://"
        std::size_t pos = modified_url.find(wss_prefix);
        if (pos == std::string::npos) {
            pos = modified_url.find(ws_prefix);
        }

        // If found, erase the substring
        if (pos != std::string::npos) {
            if (modified_url.compare(pos, wss_prefix.length(), wss_prefix) == 0) {
                modified_url.erase(pos, wss_prefix.length());
            } else if (modified_url.compare(pos, ws_prefix.length(), ws_prefix) == 0) {
                modified_url.erase(pos, ws_prefix.length());
            }
        }

        return modified_url;
    }

    /// \brief Checks if a given URL starts with a specified scheme.
    /// \param url The URL string to check.
    /// \param scheme The scheme (e.g., "http") to check.
    /// \return True if the URL starts with the specified scheme, otherwise false.
    inline bool is_valid_scheme(const std::string& url, const std::string& scheme) {
        return url.compare(0, scheme.length(), scheme) == 0;
    }
	
    /// \brief Validates if a domain name is correctly formatted.
    /// \param domain The domain string to validate.
    /// \return True if the domain is valid, otherwise false.
    inline bool is_valid_domain(const std::string& domain) {
        size_t dot_pos = domain.find('.');

        if (dot_pos == std::string::npos ||
            dot_pos == 0 ||
            dot_pos == domain.length() - 1) {
            return false;
        }

        for (char ch : domain) {
            if (!isalnum(ch) && ch != '-' && ch != '.') {
                return false;
            }
        }

        std::string tld = domain.substr(dot_pos + 1);
        for (char ch : tld) {
            if (!isalpha(ch)) {
                return false;
            }
        }
        return true;
    }

    /// \brief Checks if a path is correctly formatted.
    /// \param path The path string to validate.
    /// \return True if the path is valid, otherwise false.
    inline bool is_valid_path(const std::string& path) {
        if (path.empty() || path[0] != '/') return false;
        for (char ch : path) {
            if (!isalnum(ch) && ch != '/' && ch != '-' && ch != '_') {
                return false;
            }
        }
        return true;
    }

    /// \brief Validates if a query string is correctly formatted.
    /// \param query The query string to validate.
    /// \return True if the query string is valid, otherwise false.
    inline bool is_valid_query(const std::string& query) {
        if (query.empty() || query[0] != '?') {
            return false;
        }

        size_t pos = 1;
        while (pos < query.length()) {
            size_t equalPos = query.find('=', pos);
            size_t ampPos = query.find('&', pos);

            if (equalPos == std::string::npos || (ampPos != std::string::npos && ampPos < equalPos)) {
                return false;
            }

            pos = (ampPos == std::string::npos) ? query.length() : ampPos + 1;
        }

        return true;
    }

    /// \brief Validates if a URL is correctly formatted.
    /// \param url The URL string to validate.
    /// \param protocol A vector of valid protocol schemes.
    /// \return True if the URL is valid, otherwise false.
    inline bool is_valid_url(const std::string& url, const std::vector<std::string>& protocol) {
        size_t scheme_end = url.find("://");
        if (scheme_end == std::string::npos) {
            return false;
        }

        const std::string scheme = url.substr(0, scheme_end);
        const auto it = std::find(protocol.begin(), protocol.end(), scheme);
        if (it == protocol.end()) return false;

        const size_t domain_start = scheme_end + 3;
        const size_t path_start = url.find('/', domain_start);
        const size_t query_start = url.find('?', domain_start);

        // Определяем конец домена
        size_t domain_end = (path_start != std::string::npos) ? path_start : query_start;
        if (domain_end == std::string::npos) {
            domain_end = url.length();
        }

        const std::string domain = url.substr(domain_start, domain_end - domain_start);
        if (!is_valid_domain(domain)) {
            return false;
        }

        // Проверяем путь, если он есть
        if (path_start != std::string::npos) {
            std::string path = (query_start != std::string::npos) ? url.substr(path_start, query_start - path_start) : url.substr(path_start);
            if (!is_valid_path(path)) {
                return false;
            }
        }

        // Проверяем query, если он есть
        if (query_start != std::string::npos) {
            std::string query = url.substr(query_start);
            if (!is_valid_query(query)) {
                return false;
            }
        }

        return true;
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_URL_UTILS_HPP_INCLUDED
