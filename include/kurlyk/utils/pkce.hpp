#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_PKCE_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_PKCE_HPP_INCLUDED

/// \file pkce.hpp
/// \brief Provides PKCE (Proof Key for Code Exchange) utilities per RFC 7636.

#include "Base64Url.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

namespace kurlyk {
namespace utils {

namespace detail {

    /// \brief Fills a byte buffer with cryptographically secure random data.
    /// \param data Destination buffer.
    /// \param size Number of bytes to generate.
    inline void random_bytes(uint8_t* data, std::size_t size) {
        if (size > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
            RAND_bytes(
                reinterpret_cast<unsigned char*>(data),
                static_cast<int>(size)) != 1) {
            throw std::runtime_error("OpenSSL RAND_bytes failed");
        }
    }

    /// \brief Calculates a SHA-256 digest.
    /// \param data Input data.
    /// \param size Input size in bytes.
    /// \return SHA-256 digest bytes.
    inline std::vector<uint8_t> sha256(const void* data, std::size_t size) {
        std::vector<uint8_t> digest(EVP_MAX_MD_SIZE);
        unsigned int digest_size = 0;

        if (EVP_Digest(
                data,
                size,
                reinterpret_cast<unsigned char*>(digest.data()),
                &digest_size,
                EVP_sha256(),
                nullptr) != 1) {
            throw std::runtime_error("OpenSSL SHA-256 digest failed");
        }

        digest.resize(digest_size);
        return digest;
    }

} // namespace detail

    /// \class PkcePair
    /// \brief Stores PKCE verifier and challenge values.
    struct PkcePair {
        std::string code_verifier;          ///< Randomly generated code verifier.
        std::string code_challenge;         ///< Derived S256 code challenge.
        std::string code_challenge_method = "S256"; ///< Challenge method, always "S256".
    };

    /// \brief Generates a cryptographically strong PKCE code verifier.
    /// \param length Verifier length, must be between 43 and 128 (inclusive).
    /// \return Random string from the RFC 7636 unreserved-character set.
    inline std::string generate_code_verifier(std::size_t length = 64) {
        static const char allowed[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
        if (length < 43) length = 43;
        if (length > 128) length = 128;

        std::string verifier;
        verifier.reserve(length);

        const std::size_t allowed_count = sizeof(allowed) - 1; // exclude null terminator
        const std::size_t accepted_limit = 256 - (256 % allowed_count);

        while (verifier.size() < length) {
            const std::size_t remaining = length - verifier.size();
            std::vector<uint8_t> random_data(remaining);
            detail::random_bytes(random_data.data(), random_data.size());

            for (std::size_t i = 0; i < random_data.size() && verifier.size() < length; ++i) {
                if (random_data[i] >= accepted_limit) {
                    continue;
                }
                verifier.push_back(allowed[random_data[i] % allowed_count]);
            }
        }
        return verifier;
    }

    /// \brief Creates an S256 code challenge from a verifier.
    /// \param verifier PKCE code verifier.
    /// \return Base64url-encoded SHA256 hash without padding.
    inline std::string make_s256_code_challenge(const std::string& verifier) {
        std::vector<uint8_t> digest = detail::sha256(verifier.data(), verifier.size());
        return base64url_encode(digest.data(), digest.size());
    }

    /// \brief Creates a PKCE pair with a freshly generated verifier.
    /// \return Generated PKCE pair.
    inline PkcePair make_pkce_pair() {
        PkcePair pair;
        pair.code_verifier = generate_code_verifier();
        pair.code_challenge = make_s256_code_challenge(pair.code_verifier);
        return pair;
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_PKCE_HPP_INCLUDED
