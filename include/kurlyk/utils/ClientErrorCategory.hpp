#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_CLIENT_ERROR_CATEGORY_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_CLIENT_ERROR_CATEGORY_HPP_INCLUDED

/// \file ClientErrorCategory.hpp
/// \brief Declares the ClientError enumeration and error category for internal client-side failures.

namespace kurlyk {
namespace utils {

    /// \enum ClientError
    /// \brief Defines errors related to the internal state or lifecycle of the HTTP/WebSocket client.
    enum class ClientError {
        CancelledByUser = 1,        ///< Request was cancelled explicitly by the user via cancel().
        AbortedDuringDestruction,   ///< Request handler was destroyed before completion, causing the request to abort.
        ClientNotInitialized,       ///< Operation attempted before client was properly initialized.
        InvalidConfiguration,       ///< Provided configuration is incomplete or invalid.
        NotConnected,               ///< Operation requires an active connection but none exists.
        QueueLimitExceeded,         ///< Operation was rejected because the bounded queue is already full.
        ShuttingDown,               ///< Operation was rejected because the owning subsystem is shutting down.
    };

    /// \class ClientErrorCategory
    /// \brief Custom std::error_category for reporting internal client errors not tied to specific protocols (e.g., CURL, HTTP).
    class ClientErrorCategory : public std::error_category {
    public:
        const char* name() const noexcept override {
            return "http_client";
        }

        std::string message(int ev) const override {
            switch (static_cast<ClientError>(ev)) {
                case ClientError::CancelledByUser:
                    return "Request was cancelled by the user";
                case ClientError::AbortedDuringDestruction:
                    return "Request was aborted due to handler destruction";
                case ClientError::ClientNotInitialized:
                    return "Client was not initialized properly";
                case ClientError::InvalidConfiguration:
                    return "Invalid or missing client configuration";
                case ClientError::NotConnected:
                    return "Operation failed: client is not connected";
                case ClientError::QueueLimitExceeded:
                    return "Operation was rejected because the queue limit was exceeded";
                case ClientError::ShuttingDown:
                    return "Operation was rejected because the client is shutting down";
                default:
                    return "Unknown HTTP client error";
            }
        }
    };

    /// \brief Returns the singleton instance of the ClientErrorCategory.
    /// \return Reference to the client error category.
    inline const std::error_category& client_error_category() {
        static ClientErrorCategory instance;
        return instance;
    }

    /// \brief Creates a std::error_code from a ClientError value.
    /// \param e ClientError enum value.
    /// \return std::error_code representing the specified client error.
    inline std::error_code make_error_code(ClientError e) {
        return {static_cast<int>(e), client_error_category()};
    }

} // namespace utils
} // namespace kurlyk

/// \brief Enables use of ClientError with std::error_code.
namespace std {
    template<>
    struct is_error_code_enum<kurlyk::utils::ClientError> : true_type {};
}

#endif // KURLYK_HEADER_KURLYK_UTILS_CLIENT_ERROR_CATEGORY_HPP_INCLUDED
