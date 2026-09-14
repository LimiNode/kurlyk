#pragma once
#ifndef KURLYK_HEADER_KURLYK_UTILS_WEB_SOCKET_ERROR_CATEGORY_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_UTILS_WEB_SOCKET_ERROR_CATEGORY_HPP_INCLUDED

/// \file WebSocketErrorCategory.hpp
/// \brief Defines the WebSocketError enumeration and corresponding error category for WebSocket-level failures.

namespace kurlyk {
namespace utils {

    /// \enum WebSocketError
    /// \brief Represents protocol-level WebSocket errors.
    enum class WebSocketError {
        ConnectionFailed = 1,         ///< WebSocket connection could not be established.
        UnexpectedClose,              ///< Connection was closed unexpectedly (e.g., code 1006).
        ProtocolViolation,            ///< Protocol violation occurred during message exchange.
        UnsupportedDataType,          ///< Received an unsupported data type.
        InvalidCloseCode,             ///< Server sent an invalid close code.
        CompressionError              ///< Error occurred during compression/decompression.
    };

    /// \class WebSocketErrorCategory
    /// \brief Error category class for WebSocketError enumeration.
    class WebSocketErrorCategory : public std::error_category {
    public:
        const char* name() const noexcept override {
            return "websocket";
        }

        std::string message(int ev) const override {
            switch (static_cast<WebSocketError>(ev)) {
                case WebSocketError::ConnectionFailed:
                    return "Failed to establish WebSocket connection";
                case WebSocketError::UnexpectedClose:
                    return "WebSocket connection was closed unexpectedly";
                case WebSocketError::ProtocolViolation:
                    return "WebSocket protocol violation detected";
                case WebSocketError::UnsupportedDataType:
                    return "Unsupported WebSocket data type received";
                case WebSocketError::InvalidCloseCode:
                    return "Received invalid WebSocket close code";
                case WebSocketError::CompressionError:
                    return "Compression or decompression error during WebSocket exchange";
                default:
                    return "Unknown WebSocket error";
            }
        }
    };

    /// \brief Returns the singleton instance of the WebSocket error category.
    inline const std::error_category& websocket_error_category() {
        static WebSocketErrorCategory instance;
        return instance;
    }

    /// \brief Constructs an std::error_code from a WebSocketError.
    inline std::error_code make_error_code(WebSocketError e) {
        return {static_cast<int>(e), websocket_error_category()};
    }

} // namespace utils
} // namespace kurlyk

/// \brief Enables WebSocketError to be used with std::error_code.
namespace std {
    template<>
    struct is_error_code_enum<kurlyk::utils::WebSocketError> : true_type {};
}

#endif // KURLYK_HEADER_KURLYK_UTILS_WEB_SOCKET_ERROR_CATEGORY_HPP_INCLUDED
