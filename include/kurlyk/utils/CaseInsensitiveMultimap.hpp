#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_CASE_INSENSITIVE_MULTIMAP_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_CASE_INSENSITIVE_MULTIMAP_HPP_INCLUDED

/// \file CaseInsensitiveMultimap.hpp
/// \brief Defines case-insensitive utilities and data structures for HTTP headers, cookies, and query parameters.

namespace kurlyk {
namespace utils {

    /// \brief Compares two strings case-insensitively.
    /// \param str1 First string to compare.
    /// \param str2 Second string to compare.
    /// \return `true` if strings are equal ignoring case, otherwise `false`.
    inline bool case_insensitive_equal(const std::string& str1, const std::string& str2) noexcept {
        return str1.size() == str2.size() &&
               std::equal(str1.begin(), str1.end(), str2.begin(), [](char a, char b) {
                   return tolower(a) == tolower(b);
               });
    }

    /// \class CaseInsensitiveEqual
    /// \brief Functor for case-insensitive string comparison.
    class CaseInsensitiveEqual {
    public:
        /// \brief Compares two strings case-insensitively.
        /// \param str1 First string to compare.
        /// \param str2 Second string to compare.
        /// \return `true` if strings are equal ignoring case, otherwise `false`.
        bool operator()(const std::string& str1, const std::string& str2) const noexcept {
            return case_insensitive_equal(str1, str2);
        }
    };

    /// \class CaseInsensitiveHash
    /// \brief Functor for generating case-insensitive hash values for strings.
    class CaseInsensitiveHash {
    public:
        /// \brief Computes a case-insensitive hash value for a given string.
        /// \param str The string to hash.
        /// \return The computed hash value.
        std::size_t operator()(const std::string& str) const noexcept {
            std::size_t h = 0;
            std::hash<int> hash;
            for (auto c : str) {
                h ^= hash(tolower(c)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    /// \brief A case-insensitive unordered multimap for storing HTTP headers.
    using CaseInsensitiveMultimap = std::unordered_multimap<std::string, std::string, CaseInsensitiveHash, CaseInsensitiveEqual>;
	
	/// \brief A case-insensitive unordered multimap specifically for storing HTTP cookies.
    using CaseInsensitiveCookieMultimap = std::unordered_multimap<std::string, Cookie, CaseInsensitiveHash, CaseInsensitiveEqual>;

} // namespace utils
} // namespace kurlyk

namespace kurlyk {

    /// \brief Alias for HTTP headers, providing a case-insensitive unordered multimap.
    using Headers = utils::CaseInsensitiveMultimap;

    /// \brief Alias for query parameters in HTTP requests, stored case-insensitively.
    using QueryParams = utils::CaseInsensitiveMultimap;

    /// \brief Alias for HTTP cookies, stored case-insensitively.
    using Cookies = utils::CaseInsensitiveCookieMultimap;

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_CASE_INSENSITIVE_MULTIMAP_HPP_INCLUDED
