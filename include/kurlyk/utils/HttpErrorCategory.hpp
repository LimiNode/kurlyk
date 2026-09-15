#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_HTTP_ERROR_CATEGORY_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_HTTP_ERROR_CATEGORY_HPP_INCLUDED

/// \file HttpErrorCategory.hpp
/// \brief Defines the HttpErrorCategory class for interpreting HTTP status codes as std::error_code values.

namespace kurlyk {
namespace utils {

    /// \class HttpErrorCategory
    /// \brief Custom error category that maps HTTP status codes (e.g., 404, 500) to human-readable error messages.
    class HttpErrorCategory : public std::error_category {
    public:
        const char* name() const noexcept override {
            return "http";
        }

        std::string message(int ev) const override {
            switch (ev) {
                case 400: return "Bad Request";
                case 401: return "Unauthorized";
                case 403: return "Forbidden";
                case 404: return "Not Found";
                case 408: return "Request Timeout";
                case 429: return "Too Many Requests";
                case 500: return "Internal Server Error";
                case 502: return "Bad Gateway";
                case 503: return "Service Unavailable";
                case 504: return "Gateway Timeout";
                default:  return "HTTP Error " + std::to_string(ev);
            }
        }
    };

    /// \brief Creates an std::error_code from an HTTP status code.
    /// \param status_code The HTTP status code (e.g., 404, 500).
    /// \return Corresponding std::error_code bound to the HTTP category.
    inline std::error_code make_http_error(int status_code) {
        static HttpErrorCategory category;
        return std::error_code(status_code, category);
    }

    /// \brief Checks whether the given error code belongs to the HTTP error category.
    /// \param ec The error code to check.
    /// \return True if the error code belongs to the "http" category.
    inline bool is_http_error(const std::error_code& ec) {
        return ec.category().name() == std::string("http");
    }

} // namespace utils
} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_UTILS_HTTP_ERROR_CATEGORY_HPP_INCLUDED
