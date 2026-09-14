#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_PERCENT_ENCODING_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_PERCENT_ENCODING_HPP_INCLUDED

/// \file percent_encoding.hpp
/// \brief Provides functions for percent-encoding and decoding strings.
///
/// This header defines utilities for performing percent-encoding (also known as URL encoding)
/// and decoding according to [RFC 3986](https://datatracker.ietf.org/doc/html/rfc3986). These
/// functions are commonly used to encode query parameters or path segments in HTTP URLs.

namespace kurlyk {
namespace utils {

    /// \brief Encodes a string using Percent Encoding according to RFC 3986.
    /// \param value The string to be encoded.
    /// \return The percent-encoded string.
    inline std::string percent_encode(const std::string &value) noexcept {
        static const char hex_chars[] = "0123456789ABCDEF";

        std::string result;
        result.reserve(value.size()); // Reserve minimum required size

        for (auto &chr : value) {
            if (isalnum(static_cast<unsigned char>(chr)) || chr == '-' || chr == '.' || chr == '_' || chr == '~') {
                result += chr;
            } else {
                result += '%';
                result += hex_chars[static_cast<unsigned char>(chr) >> 4];
                result += hex_chars[static_cast<unsigned char>(chr) & 0x0F];
            }
        }

        return result;
    }

    /// \brief Decodes a Percent-Encoded string.
    /// \param value The percent-encoded string to be decoded.
    /// \return The decoded string.
    inline std::string percent_decode(const std::string &value) noexcept {
        std::string result;
        result.reserve(value.size() / 3 + (value.size() % 3)); // Reserve minimum required size

        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '%' && i + 2 < value.size()) {
                char hex[] = { value[i + 1], value[i + 2], '\0' };
                char decoded_chr = static_cast<char>(std::strtol(hex, nullptr, 16));
                result += decoded_chr;
                i += 2;
            } else if (value[i] == '+') {
                result += ' ';
            } else {
                result += value[i];
            }
        }

        return result;
    }

} // namespace utils
} // namespace kurlyk

#endif
