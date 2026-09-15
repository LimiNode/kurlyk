#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_CURL_ERROR_CATEGORY_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_CURL_ERROR_CATEGORY_HPP_INCLUDED

/// \file CurlErrorCategory.hpp
/// \brief Defines the CurlErrorCategory class for handling CURL error codes.

namespace kurlyk {
namespace utils {

    /// \class CurlErrorCategory
    /// \brief Represents a custom error category for CURL errors, enabling integration with std::error_code.
    class CurlErrorCategory : public std::error_category {
    public:
        /// \brief Returns the name of the error category.
        /// \return A C-string representing the name of this error category ("curl").
        const char* name() const noexcept override {
            return "curl";
        }

        /// \brief Returns a descriptive error message for a given CURL error code.
        /// \param ev The integer value of the CURL error code.
        /// \return A string describing the error associated with the specified CURL error code.
        std::string message(int ev) const override {
            return curl_easy_strerror(static_cast<CURLcode>(ev));
        }
    };

    /// \brief Creates an std::error_code from a CURLcode, allowing CURL errors to be used with the std::error_code API.
    /// \param e The CURL error code.
    /// \return An std::error_code corresponding to the specified CURL error.
    inline std::error_code make_error_code(CURLcode e) {
        static CurlErrorCategory category;
        return {static_cast<int>(e), category};
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_CURL_ERROR_CATEGORY_HPP_INCLUDED
