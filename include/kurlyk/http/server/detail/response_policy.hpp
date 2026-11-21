#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_SERVER_DETAIL_RESPONSE_POLICY_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_SERVER_DETAIL_RESPONSE_POLICY_HPP_INCLUDED

#include <kurlyk/http/server/data.hpp>
#include <algorithm>

namespace kurlyk {
namespace detail {

// Backend-independent result of the common HTTP response policy.
struct HttpServerResponseFraming {
    bool emit_content_length;
    std::size_t content_length;
    bool emit_body;
};

inline bool http_server_header_equal(const std::string& left, const char* right) {
    std::size_t index = 0;
    for (; index < left.size() && right[index] != '\0'; ++index) {
        unsigned char byte = static_cast<unsigned char>(left[index]);
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<unsigned char>(byte + ('a' - 'A'));
        if (byte != static_cast<unsigned char>(right[index])) return false;
    }
    return index == left.size() && right[index] == '\0';
}

inline HttpServerResponseFraming prepare_http_server_response(
        const HttpServerConfig& config, const std::string& method, HttpServerResponse& response) {
    std::size_t bytes = 0;
    bool valid = response.status >= 200 && response.status <= 599 &&
        response.body.size() <= config.max_response_body_bytes;
    for (const auto& header : response.headers) {
        const auto& name = header.first;
        const auto& value = header.second;
        const bool invalid_value = std::any_of(value.begin(), value.end(), [](unsigned char byte) {
            return (byte < 0x20 && byte != '\t') || byte == 0x7f;
        });
        const std::size_t remaining = config.max_response_header_bytes - bytes;
        if (name.empty() || name.find_first_not_of("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos ||
            invalid_value || name.size() > remaining ||
            value.size() > remaining - name.size() || remaining - name.size() - value.size() < 4 ||
            http_server_header_equal(name, "content-length") ||
            http_server_header_equal(name, "transfer-encoding") ||
            http_server_header_equal(name, "connection")) { valid = false; break; }
        bytes += name.size() + value.size() + 4;
    }
    if (!valid) { response = HttpServerResponse(); response.status = 500; }
    const bool no_body = response.status == 204 || response.status == 205 || response.status == 304;
    return HttpServerResponseFraming{
        response.status != 204 && response.status != 304,
        no_body ? 0 : response.body.size(),
        method != "HEAD" && !no_body};
}

} // namespace detail
} // namespace kurlyk
#endif
