#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_PROXY_CHECKER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_PROXY_CHECKER_HPP_INCLUDED

/// \file ProxyChecker.hpp
/// \brief Defines an asynchronous HTTP-based proxy checker.

#include <future>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <kurlyk/core.hpp>
#include "data.hpp"
#include "HttpRequestManager.hpp"
#include "utils.hpp"

namespace kurlyk {
namespace detail {

    inline bool proxy_check_has_scheme(const std::string& url, const char* scheme) {
        const std::string prefix(scheme);
        if (url.size() < prefix.size()) return false;
        for (std::string::size_type i = 0; i < prefix.size(); ++i) {
            char actual = url[i];
            if (actual >= 'A' && actual <= 'Z') {
                actual = static_cast<char>(actual - 'A' + 'a');
            }
            if (actual != prefix[i]) return false;
        }
        return true;
    }

    inline bool proxy_check_is_http_url(const std::string& url) {
        return proxy_check_has_scheme(url, "http://") ||
               proxy_check_has_scheme(url, "https://");
    }

    inline long proxy_check_milliseconds(double seconds) {
        if (seconds <= 0.0) return 0;
        const double milliseconds = seconds * 1000.0;
        const double maximum = static_cast<double>((std::numeric_limits<long>::max)());
        if (milliseconds >= maximum) {
            return (std::numeric_limits<long>::max)();
        }
        return static_cast<long>(milliseconds + 0.5);
    }

    inline ProxyCheckResult make_proxy_check_error(
            const std::error_code& error_code,
            const std::string& error_message = std::string()) {
        ProxyCheckResult result;
        result.error_code = error_code;
        result.error_message = error_message.empty() ? error_code.message() : error_message;
        return result;
    }

    inline ProxyCheckResult make_proxy_check_result(
            const HttpResponsePtr& response,
            bool is_https) {
        if (!response) {
            return make_proxy_check_error(
                utils::make_error_code(utils::ClientError::AbortedDuringDestruction),
                "Proxy check returned no HTTP response");
        }

        ProxyCheckResult result;
        result.error_code = response->error_code;
        result.error_message = response->error_message.empty() && response->error_code
            ? response->error_code.message()
            : response->error_message;
        result.http_status = response->status_code;

        const bool received_http_response =
            response->ready &&
            response->status_code > 0 &&
            (!response->error_code || utils::is_http_error(response->error_code));
        result.reachable = received_http_response;
        result.http_ok = received_http_response &&
            response->status_code >= 200 && response->status_code < 400;
        result.https_ok = is_https && result.http_ok;

        result.connect_latency_ms = proxy_check_milliseconds(response->connect_time);
        if (response->appconnect_time > 0.0 &&
            response->connect_time >= 0.0 &&
            response->appconnect_time >= response->connect_time) {
            result.tls_latency_ms = proxy_check_milliseconds(
                response->appconnect_time - response->connect_time);
        }
        result.ttfb_ms = proxy_check_milliseconds(response->starttransfer_time);
        result.total_latency_ms = proxy_check_milliseconds(response->total_time);
        return result;
    }

    inline void set_proxy_check_promise(
            const std::shared_ptr<std::promise<ProxyCheckResult>>& promise,
            ProxyCheckResult result) {
        try {
            promise->set_value(std::move(result));
        } catch (const std::future_error& error) {
            KURLYK_HANDLE_ERROR(error, "Future error in ProxyChecker callback");
        } catch (const std::exception& error) {
            KURLYK_HANDLE_ERROR(error, "Unhandled exception in ProxyChecker callback");
        } catch (...) {
            // Unknown fatal error in proxy checker callback.
        }
    }

} // namespace detail

    /// \class ProxyChecker
    /// \brief Checks whether a proxy can complete an HTTP or HTTPS no-body request.
    class ProxyChecker {
    public:
        /// \brief Starts an asynchronous proxy check.
        /// \param proxy Passive proxy configuration to check. The `use` flag is not consulted.
        /// \param options Test URL, timeouts, redirect behavior, and tunnel mode.
        /// \return Future containing availability, status, errors, and timing metrics.
        /// \note The check uses one request without retries and does not download a response body.
        std::future<ProxyCheckResult> check(
                const ProxyConfig& proxy,
                const ProxyCheckOptions& options = ProxyCheckOptions()) const {
            const std::shared_ptr<std::promise<ProxyCheckResult>> promise(
                new std::promise<ProxyCheckResult>());
            std::future<ProxyCheckResult> future = promise->get_future();

            if (!proxy.is_valid()) {
                detail::set_proxy_check_promise(
                    promise,
                    detail::make_proxy_check_error(
                        utils::make_error_code(utils::ClientError::InvalidConfiguration),
                        "Proxy server must contain a valid host and port"));
                return future;
            }

            if (!detail::proxy_check_is_http_url(options.test_url) ||
                options.connect_timeout < 0 ||
                options.request_timeout < 0) {
                detail::set_proxy_check_promise(
                    promise,
                    detail::make_proxy_check_error(
                        utils::make_error_code(utils::ClientError::InvalidConfiguration),
                        "Proxy check requires an HTTP(S) test URL and non-negative timeouts"));
                return future;
            }

#           if __cplusplus >= 201402L
            std::unique_ptr<HttpRequest> request = std::make_unique<HttpRequest>();
#           else
            std::unique_ptr<HttpRequest> request(new HttpRequest());
#           endif
            request->url = options.test_url;
            request->method = "HEAD";
            request->head_only = true;
            request->ca_file = options.ca_file;
            request->follow_location = options.follow_redirects;
            request->proxy_server = proxy.proxy_server;
            request->proxy_auth = proxy.proxy_auth;
            request->proxy_type = proxy.proxy_type;
            request->proxy_tunnel = options.proxy_tunnel;
            request->connect_timeout = options.connect_timeout;
            request->timeout = options.request_timeout;
            request->retry_attempts = 0;

            const bool is_https = detail::proxy_check_has_scheme(options.test_url, "https://");
            HttpResponseCallback callback = [promise, is_https](HttpResponsePtr response) {
                if (response && !response->ready) return;
                detail::set_proxy_check_promise(
                    promise,
                    detail::make_proxy_check_result(response, is_https));
            };

            const SubmitResult submit_result = submit_http_request(
                std::move(request), std::move(callback));
            if (!submit_result) {
                detail::set_proxy_check_promise(
                    promise,
                    detail::make_proxy_check_error(submit_result.error_code));
            }
            return future;
        }
    };

    /// \brief Starts an asynchronous no-body request through a proxy.
    /// \param proxy Passive proxy configuration to check.
    /// \param options Test URL and request behavior.
    /// \return Future containing the proxy check result.
    inline std::future<ProxyCheckResult> check_proxy(
            const ProxyConfig& proxy,
            const ProxyCheckOptions& options = ProxyCheckOptions()) {
        return ProxyChecker().check(proxy, options);
    }

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_PROXY_CHECKER_HPP_INCLUDED
