#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_ENCODING_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_ENCODING_UTILS_HPP_INCLUDED

/// \file encoding_utils.hpp
/// \brief Provides character encoding conversion utilities (e.g., UTF-8 to ANSI).

namespace kurlyk {
namespace utils {

#   if defined(_WIN32)
    /// \brief Converts a UTF-8 string to an ANSI string (Windows-specific).
    /// \param utf8 The UTF-8 encoded string.
    /// \return The converted ANSI string.
    inline std::string utf8_to_ansi(const std::string& utf8) noexcept {
        int n_len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
        if (n_len == 0) return {};

        std::wstring wide_string(n_len + 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide_string[0], n_len);

        n_len = WideCharToMultiByte(CP_ACP, 0, wide_string.c_str(), -1, NULL, 0, NULL, NULL);
        if (n_len == 0) return {};

        std::string ansi_string(n_len - 1, '\0');
        WideCharToMultiByte(CP_ACP, 0, wide_string.c_str(), -1, &ansi_string[0], n_len, NULL, NULL);
        return ansi_string;
    }
#   endif

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_ENCODING_UTILS_HPP_INCLUDED
