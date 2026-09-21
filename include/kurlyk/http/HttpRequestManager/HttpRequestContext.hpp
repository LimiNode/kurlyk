#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_REQUEST_CONTEXT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_REQUEST_CONTEXT_HPP_INCLUDED

/// \file HttpRequestContext.hpp
/// \brief Defines the HttpRequestContext class for managing HTTP request context, including retries and timing.

namespace kurlyk {

    /// \class HttpRequestContext
    /// \brief Represents the context of an HTTP request, including the request object, callback function, retry attempts, and timing.
    class HttpRequestContext {
    public:
        using time_point_t = std::chrono::steady_clock::time_point;

        std::unique_ptr<HttpRequest> request;       ///< The HTTP request associated with this context.
        HttpResponseCallback         callback;      ///< Callback function to be invoked when the request completes.
        long                         retry_attempt; ///< Number of retry attempts made for this request.
        time_point_t                 start_time;    ///< Time when the request was initially created or last retried.
        uint64_t                     in_flight_token = 0; ///< Token for sequential rate-limit tracking.
        std::function<void()>        on_complete;   ///< Callback invoked once when the request finishes (including retries).
        std::function<void()>        on_group_complete; ///< Callback invoked after the final user callback for the group.
        std::atomic<bool>            complete_called{false};  ///< True after on_complete has been invoked.

        /// \brief Constructs a HttpRequestContext with the specified request and callback.
        /// \param request_ptr A unique pointer to the HTTP request object.
        /// \param callback Callback function to be invoked upon request completion.
        HttpRequestContext(
            std::unique_ptr<HttpRequest> request_ptr,
            HttpResponseCallback callback)
            : request(std::move(request_ptr)),
              callback(std::move(callback)),
              retry_attempt(0),
              in_flight_token(0),
              complete_called(false) {
        }

        HttpRequestContext() = default;

        /// \brief Invokes on_complete exactly once. Thread-safe and idempotent.
        void complete() {
            bool expected = false;
            if (!complete_called.compare_exchange_strong(expected, true)) {
                return;
            }
            if (on_complete) {
                on_complete();
            }
            if (on_group_complete) {
                on_group_complete();
            }
        }
    }; // HttpRequestContext

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_REQUEST_CONTEXT_HPP_INCLUDED
