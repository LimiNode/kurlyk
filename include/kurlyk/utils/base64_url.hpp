#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_BASE64_URL_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_BASE64_URL_HPP_INCLUDED

/// \file base64_url.hpp
/// \brief Provides Base64url encoding and decoding (RFC 4648, no padding).

#include <string>
#include <vector>
#include <cstdint>

namespace kurlyk {
namespace utils {

    /// \brief Encodes a byte buffer using Base64url (RFC 4648) without padding.
    /// \param data Input data buffer.
    /// \param length Length of the input buffer.
    /// \return Base64url-encoded string.
    inline std::string base64url_encode(const uint8_t* data, std::size_t length) {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        std::string result;
        result.reserve(((length + 2) / 3) * 4);

        std::size_t i = 0;
        while (i + 2 < length) {
            uint32_t b = (static_cast<uint32_t>(data[i]) << 16)
                       | (static_cast<uint32_t>(data[i + 1]) << 8)
                       | static_cast<uint32_t>(data[i + 2]);
            result.push_back(table[(b >> 18) & 0x3F]);
            result.push_back(table[(b >> 12) & 0x3F]);
            result.push_back(table[(b >> 6) & 0x3F]);
            result.push_back(table[b & 0x3F]);
            i += 3;
        }

        if (i + 1 == length) {
            uint32_t b = static_cast<uint32_t>(data[i]) << 16;
            result.push_back(table[(b >> 18) & 0x3F]);
            result.push_back(table[(b >> 12) & 0x3F]);
        } else if (i + 2 == length) {
            uint32_t b = (static_cast<uint32_t>(data[i]) << 16)
                       | (static_cast<uint32_t>(data[i + 1]) << 8);
            result.push_back(table[(b >> 18) & 0x3F]);
            result.push_back(table[(b >> 12) & 0x3F]);
            result.push_back(table[(b >> 6) & 0x3F]);
        }

        return result;
    }

    /// \brief Encodes a string using Base64url (RFC 4648) without padding.
    /// \param value Input string.
    /// \return Base64url-encoded string.
    inline std::string base64url_encode(const std::string& value) {
        return base64url_encode(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }

    /// \brief Decodes a Base64url string (RFC 4648, no padding) into bytes.
    /// \param value Base64url-encoded string.
    /// \return Decoded byte vector.
    inline std::vector<uint8_t> base64url_decode(const std::string& value) {
        static const int8_t decode_table[] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
            -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
            -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
        };

        std::string padded = value;
        while (padded.size() % 4 != 0) {
            padded.push_back('=');
        }

        std::vector<uint8_t> result;
        result.reserve((padded.size() / 4) * 3);

        std::size_t i = 0;
        while (i < padded.size()) {
            int8_t c0 = decode_table[static_cast<uint8_t>(padded[i])];
            int8_t c1 = decode_table[static_cast<uint8_t>(padded[i + 1])];
            int8_t c2 = decode_table[static_cast<uint8_t>(padded[i + 2])];
            int8_t c3 = decode_table[static_cast<uint8_t>(padded[i + 3])];

            if (c0 < 0 || c1 < 0) break;
            if (c2 < 0 && padded[i + 2] != '=') break;
            if (c3 < 0 && padded[i + 3] != '=') break;

            uint32_t b = (static_cast<uint32_t>(c0) << 18)
                       | (static_cast<uint32_t>(c1) << 12)
                       | ((c2 >= 0 ? static_cast<uint32_t>(c2) : 0u) << 6)
                       | (c3 >= 0 ? static_cast<uint32_t>(c3) : 0u);

            result.push_back(static_cast<uint8_t>((b >> 16) & 0xFF));
            if (padded[i + 2] != '=') {
                result.push_back(static_cast<uint8_t>((b >> 8) & 0xFF));
            }
            if (padded[i + 3] != '=') {
                result.push_back(static_cast<uint8_t>(b & 0xFF));
            }
            i += 4;
        }

        return result;
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_BASE64_URL_HPP_INCLUDED
