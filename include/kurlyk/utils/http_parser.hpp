#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_HTTP_PARSER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_HTTP_PARSER_HPP_INCLUDED

/// \file http_parser.hpp
/// \brief Provides utility functions for parsing HTTP headers and cookies.

namespace kurlyk {
namespace utils {

    /// \brief Parses a header pair from a buffer.
    /// \param buffer The buffer containing the header.
    /// \param size The size of the buffer.
    /// \param key Output parameter for the header key.
    /// \param value Output parameter for the header value.
    inline void parse_http_header_pair(
            const char *buffer,
            const size_t& size,
            std::string& key,
            std::string& value) {
        std::string header(buffer, size);
        if (header.size() < 3) return;
        std::size_t colon_pos = header.find_first_of(":");
        std::size_t start_pos = colon_pos + 1;
        if (colon_pos == std::string::npos || colon_pos == 0) return;
        key = header.substr(0, colon_pos);
        std::size_t end_pos = header.find_first_not_of(" ", start_pos);
        value = (end_pos != std::string::npos) ? header.substr(end_pos) : std::string();

        // Trim newline characters from value
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
        }
    }
    
#   ifdef KURLYK_USE_CURL

    /// \brief Converts a map of query parameters into a URL query string.
    /// \param query The multimap containing query fields and values.
    /// \param prefix Optional prefix for the query string.
    /// \return The encoded query string.
    inline std::string to_query_string(
            const QueryParams &query,
            const std::string &prefix = std::string()) noexcept {
        if (query.empty()) return std::string();
        std::string query_string(prefix);
        CURL *curl = curl_easy_init();
        if (!curl) return query_string;

        size_t index = 0;
        const size_t index_end = query.size() - 1;
        for(const auto& pair : query) {
            char *escaped_key = curl_easy_escape(curl, pair.first.c_str(), pair.first.size());
            if (escaped_key) {
                query_string += std::string(escaped_key);
                curl_free(escaped_key);
            }
            query_string += "=";
            char *escaped_value = curl_easy_escape(curl, pair.second.c_str(), pair.second.size());
            if(escaped_value) {
                query_string += std::string(escaped_value);
                curl_free(escaped_value);
            }
            if (index != index_end) query_string += "&";
            ++index;
        }
        curl_easy_cleanup(curl);
        return query_string;
    }

#   else

    /// \brief Converts a map of query parameters into a URL query string.
    /// \param query The multimap containing query fields and values.
    /// \param prefix Optional prefix for the query string.
    /// \return The encoded query string.
    inline std::string to_query_string(
            const QueryParams &query,
            const std::string &prefix = std::string()) noexcept {
        std::string result(prefix);
        bool first = true;

        for (const auto &field : query) {
            if (!first) {
                result += '&';
            }
            result += field.first + '=' + percent_encode(field.second);
            first = false;
        }

        return result;
    }

#   endif

    /// \brief Converts a CaseInsensitiveMultimap to a string format suitable for HTTP Cookie headers.
    /// \param cookies The multimap containing key-value pairs.
    /// \return A string formatted as a Cookie header.
    inline std::string to_cookie_string(const CaseInsensitiveMultimap& cookies) {
        std::string result;

        for (auto it = cookies.begin(); it != cookies.end(); ++it) {
            if (it != cookies.begin()) {
                result += "; ";
            }
            result += it->first + "=" + it->second;
        }

        return result;
    }

    /// \brief Converts a CaseInsensitiveMultimap to a string format suitable for HTTP Cookie headers.
    /// \param cookies The multimap containing key-value pairs.
    /// \return A string formatted as a Cookie header.
    inline std::string to_cookie_string(const Cookies& cookies) {
        std::string result;

        for (auto it = cookies.begin(); it != cookies.end(); ++it) {
            if (it != cookies.begin()) {
                result += "; ";
            }
            result += it->second.name + "=" + it->second.value;

            if (!it->second.path.empty()) {
                result += "; Path=" + it->second.path;
            }

            if (it->second.expiration_date != 0) {
                result += "; Expires=" + std::to_string(it->second.expiration_date);
            }
        }

        return result;
    }

    /// \brief Parses a cookie string into a Cookies object.
    /// \param cookie The cookie string to parse.
    /// \return A Cookies object containing parsed cookies.
    inline Cookies parse_cookie(std::string cookie) {
        Cookies cookies;
        std::vector<std::string> list_fragment;
        cookie += ";";
        const std::string secure_prefix("__Secure-");
        const std::string host_prefix("__Host-");
        std::size_t start_pos = 0;

        for (;;) {
            bool is_option = false;
            std::size_t separator_pos = cookie.find_first_of("=;", start_pos);
            if (separator_pos != std::string::npos) {
                std::string name = cookie.substr(start_pos, (separator_pos - start_pos));

                if (name.size() > host_prefix.size() && name.substr(0, 2) == "__") {
                    std::size_t prefix_pos = name.find_first_of("-");
                    if(prefix_pos != std::string::npos) {
                        name = name.substr(prefix_pos + 1, name.size() - prefix_pos - 1);
                    }
                } else {
                    if (case_insensitive_equal(name, "expires") ||
                        case_insensitive_equal(name, "max-age") ||
                        case_insensitive_equal(name, "path") ||
                        case_insensitive_equal(name, "domain") ||
                        case_insensitive_equal(name, "samesite")) {
                        is_option = true;
                    } else if (case_insensitive_equal(name, "secure") ||
                               case_insensitive_equal(name, "httponly")) {
                        start_pos = (cookie[separator_pos] == ';') ? separator_pos + 2 : separator_pos + 1;
                        continue;
                    }
                }

                std::size_t end_pos = cookie.find("; ", separator_pos + 1);
                if (end_pos == std::string::npos) {
                    std::string value = cookie.substr(separator_pos + 1, cookie.size() - separator_pos - 2);
                    Cookie cookie_obj{name, value};
                    if (!is_option) cookies.emplace(cookie_obj.name, cookie_obj);
                    break;
                }
                std::string value = cookie.substr(separator_pos + 1, end_pos - separator_pos - 1);
                start_pos = end_pos + 2;
                Cookie cookie_obj{name, value};
                if (!is_option) cookies.emplace(cookie_obj.name, cookie_obj);
            } else {
                break;
            }
        }
        return cookies;
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_HTTP_PARSER_HPP_INCLUDED
