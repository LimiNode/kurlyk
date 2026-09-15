#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_EMAIL_UTILS_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_EMAIL_UTILS_HPP_INCLUDED

/// \file email_utils.hpp
/// \brief Provides utility functions for validating email address formats.

namespace kurlyk {
namespace utils {

    /// \brief Validates an email address format.
    /// \param str The email address to validate.
    /// \return True if the email address format is valid, false otherwise.
    inline bool is_valid_email_id(const std::string &str) {
        // Regular expression to validate an email address format
        static const std::regex email_regex(
            R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)"
        );
        return std::regex_match(str, email_regex);
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_EMAIL_UTILS_HPP_INCLUDED
