#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_HTTP_CLIENT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_HTTP_CLIENT_HPP_INCLUDED

/// \file HttpClient.hpp
/// \brief Contains the definition of the concrete HttpClient class for making HTTP requests to a specific host.

#if KURLYK_AUTH_SUPPORT
#   include "auth/IAuthProvider.hpp"
#endif

namespace kurlyk {

    /// \class HttpClient
    /// \brief Concrete HTTP client for making requests to a specific host.
    /// Provides configuration helpers for rate limiting, proxy settings, retry logic, and request defaults.
    class HttpClient {
    public:

        /// \brief Default constructor for HttpClient.
        HttpClient() {
            ensure_initialized();
            m_request.group_id = HttpRequestManager::get_instance().generate_group_id();
        }

        /// \brief Constructs an HttpClient with the specified host.
        /// \param host The base host URL for the HTTP client.
        HttpClient(const std::string& host) :
                m_host(host) {
            ensure_initialized();
            m_request.group_id = HttpRequestManager::get_instance().generate_group_id();
        }

        HttpClient(const HttpClient&) = delete;
        HttpClient& operator=(const HttpClient&) = delete;
        HttpClient(HttpClient&&) = delete;
        HttpClient& operator=(HttpClient&&) = delete;

        /// \brief Destroys the client, cancels requests associated with this client, and releases owned rate limits.
        /// \warning This method blocks until cancellation callback is delivered.
        /// It must not be called from the network worker thread.
        ~HttpClient() {
            cancel_requests();
            clear_rate_limit(RateLimitType::RL_GENERAL);
            clear_rate_limit(RateLimitType::RL_SPECIFIC);
        }

        /// \brief Cancels requests associated with this client and waits for cancellation callbacks.
        /// \note All requests created by one HttpClient share the same group_id.
        /// \warning This method blocks until cancellation callback is delivered.
        /// It must not be called from the network worker thread.
        void cancel_requests() {
            auto& worker = core::NetworkWorker::get_instance();
            auto& manager = HttpRequestManager::get_instance();
            const uint64_t group_id = m_request.group_id;
            if (worker.is_worker_thread()) {
                // On the worker thread: execute cancellation inline to avoid self-deadlock.
                manager.cancel_requests_by_group_id(group_id, nullptr);
                worker.process();
                return;
            }
            auto promise = std::make_shared<std::promise<void>>();
            auto future = promise->get_future();
            manager.cancel_requests_by_group_id(group_id, [promise](){
                try {
                    promise->set_value();
                } catch (const std::future_error& e) {
                    if (e.code() == std::make_error_condition(std::future_errc::promise_already_satisfied)) {
                        KURLYK_HANDLE_ERROR(e, "Promise already satisfied in HttpClient::request callback");
                    } else {
                        KURLYK_HANDLE_ERROR(e, "Future error in HttpClient::request callback");
                    }
                } catch (const std::exception& e) {
                    KURLYK_HANDLE_ERROR(e, "Unhandled exception in HttpClient::request callback");
                } catch (...) {
                    // Unknown fatal error in request callback
                }
            });
            worker.notify();
            try {
                future.get();
            } catch (const std::exception& e) {
                KURLYK_HANDLE_ERROR(e, "cancel_requests() future.get() failed");
            }
        }

        /// \brief Waits until all requests associated with this client group finish.
        /// \warning Blocks until all callbacks for this client's group are delivered.
        /// Must not be called from the network worker thread.
        /// \throws std::logic_error If called from the network worker thread.
        void wait_requests() {
            auto& worker = core::NetworkWorker::get_instance();
            if (worker.is_worker_thread()) {
                throw std::logic_error("HttpClient::wait_requests() must not be called from the network worker thread");
            }
            auto future = make_wait_requests_future(nullptr);
            worker.notify();
            try {
                future.get();
            } catch (const std::exception& e) {
                KURLYK_HANDLE_ERROR(e, "wait_requests() future.get() failed");
            }
        }

        /// \brief Waits until all requests associated with this client group finish or timeout expires.
        /// \param timeout Maximum time to wait.
        /// \return True if all requests finished; false on timeout or when called from the network worker thread.
        /// \warning Must not be called from the network worker thread.
        bool wait_requests_for(std::chrono::milliseconds timeout) {
            auto& worker = core::NetworkWorker::get_instance();
            if (worker.is_worker_thread()) {
                return false;
            }
            uint64_t waiter_id = 0;
            auto future = make_wait_requests_future(&waiter_id);
            worker.notify();
            if (future.wait_for(timeout) == std::future_status::timeout) {
                HttpRequestManager::get_instance().cancel_wait_requests_by_group_id(
                    m_request.group_id,
                    waiter_id);
                return false;
            }
            try {
                future.get();
            } catch (const std::exception& e) {
                KURLYK_HANDLE_ERROR(e, "wait_requests_for() future.get() failed");
                return false;
            }
            return true;
        }

        /// \brief Sets maximum number of requests this client may submit while its group is busy.
        /// \param max_in_flight Maximum observed managed requests for this client's group. `0` disables the limit.
        /// \note This is a per-client admission guard. It is checked before this HttpClient
        /// submits a request and does not enforce a global per-group invariant for requests
        /// submitted directly through HttpRequestManager or other producers.
        /// \note Concurrent submissions through the same HttpClient are serialized for this
        /// admission check.
        void set_max_in_flight(std::size_t max_in_flight) {
            std::lock_guard<std::mutex> lock(m_submit_mutex);
            m_max_in_flight = max_in_flight;
        }

        /// \brief Returns this client's configured admission cap.
        /// \return Configured per-client admission cap, or `0` if disabled.
        std::size_t max_in_flight() const {
            std::lock_guard<std::mutex> lock(m_submit_mutex);
            return m_max_in_flight;
        }

        /// \brief Returns the number of admitted requests whose final callback is still pending for this group.
        /// \return Number of outstanding requests with this client's group ID.
        /// \note This is a snapshot of HttpRequestManager state and may change immediately in concurrent code.
        std::size_t in_flight_requests() const {
            return HttpRequestManager::get_instance().group_request_count(m_request.group_id);
        }

        /// \brief Clears the configured rate limit of the specified type.
        /// \param type Rate limit type to clear.
        void clear_rate_limit(RateLimitType type = RateLimitType::RL_GENERAL) {
            auto& instance = HttpRequestManager::get_instance();

            switch (type) {
            case RateLimitType::RL_GENERAL:
                if (m_owns_general_rate_limit) {
                    instance.remove_limit(m_request.general_rate_limit);
                }
                m_request.general_rate_limit.reset();
                m_owns_general_rate_limit = false;
                break;

            case RateLimitType::RL_SPECIFIC:
                if (m_owns_specific_rate_limit) {
                    instance.remove_limit(m_request.specific_rate_limit);
                }
                m_request.specific_rate_limit.reset();
                m_owns_specific_rate_limit = false;
                break;
            }
        }

        /// \brief Sets the host URL for the HTTP client.
        /// \param host The base host URL for HTTP requests.
        void set_host(const std::string& host) {
            m_host = host;
        }

        /// \brief Sets the default headers for HTTP requests.
        /// \param headers The headers to be included with each request.
        void set_headers(const kurlyk::Headers& headers) {
            m_request.headers = headers;
        }

#if KURLYK_AUTH_SUPPORT
        /// \brief Assigns an authentication provider to all requests created by this client.
        /// \param provider Shared pointer to an IAuthProvider implementation.
        /// \note The provider's `authorize()` is called on every request created by this client
        ///       after per-request headers are merged, so it can overwrite headers such as
        ///       `Authorization` set manually on the request. Set to `nullptr` to disable.
        /// \note Thread-safety: this setter is not synchronized; call it only before concurrent
        ///       requests begin, or externally synchronize with request submission.
        void set_auth_provider(std::shared_ptr<http::auth::IAuthProvider> provider) {
            m_auth_provider = provider;
        }

        /// \brief Removes the current authentication provider from this client.
        void clear_auth_provider() {
            m_auth_provider.reset();
        }

        /// \brief Returns whether this client has an active authentication provider.
        bool has_auth_provider() const {
            return m_auth_provider != nullptr;
        }

        /// \brief Returns the current authentication provider, or nullptr if none is set.
        std::shared_ptr<http::auth::IAuthProvider> auth_provider() const {
            return m_auth_provider;
        }
#endif

        /// \brief Assigns an existing rate limit to future requests by ID.
        /// \param limit_id The unique identifier of the rate limit to assign.
        /// \param type Rate limit type to configure.
        /// \return True if the rate limit was found and assigned; false otherwise.
        bool assign_rate_limit_id(
                long limit_id,
                RateLimitType type = RateLimitType::RL_GENERAL) {
            return assign_rate_limit_handle(
                HttpRequestManager::get_instance().get_rate_limit(limit_id), type);
        }

        /// \brief Assigns an existing rate-limit handle to future requests.
        /// \param limit Shared rate-limit handle to assign.
        /// \param type Rate limit type to configure.
        /// \return True if the handle was assigned; false if the handle is empty.
        /// \note The client does not own externally assigned limits and will not release
        ///       the manager-owned handle for them.
        bool assign_rate_limit_handle(
                const HttpRateLimitHandlePtr& limit,
                RateLimitType type = RateLimitType::RL_GENERAL) {
            if (!limit) {
                return false;
            }

            clear_rate_limit(type);

            switch (type) {
            case RateLimitType::RL_GENERAL:
                m_request.general_rate_limit = limit;
                break;

            case RateLimitType::RL_SPECIFIC:
                m_request.specific_rate_limit = limit;
                break;
            }
            return true;
        }

        /// \brief Sets the rate limit ID for the HTTP request (alias for `assign_rate_limit_id`).
        /// \param limit_id The unique identifier of the rate limit to assign.
        /// \param type Specifies the rate limit type (general or specific).
        /// \return True if the rate limit was found and assigned; false otherwise.
        /// \note This method is an alias for `assign_rate_limit_id`.
        bool set_rate_limit_id(
                long limit_id,
                RateLimitType type = RateLimitType::RL_GENERAL) {
            return assign_rate_limit_id(limit_id, type);
        }

        /// \brief Sets an existing rate-limit handle for future requests.
        /// \param limit Shared rate-limit handle to assign.
        /// \param type Rate limit type to configure.
        /// \return True if the handle was assigned; false if the handle is empty.
        bool set_rate_limit_handle(
                const HttpRateLimitHandlePtr& limit,
                RateLimitType type = RateLimitType::RL_GENERAL) {
            return assign_rate_limit_handle(limit, type);
        }

        /// \brief Creates and assigns an owned rate limit for future requests.
        /// \param requests_per_period Maximum requests allowed within the period. `0` means unlimited.
        /// \param period_ms Period duration in milliseconds.
        /// \param type Rate limit type to configure.
        /// \param sequential When true, no other request sharing this limit may start until
        ///        the current request (including all its retries) has finished.
        void set_rate_limit(
                long requests_per_period,
                long period_ms,
                RateLimitType type = RateLimitType::RL_GENERAL,
                bool sequential = false) {
            auto& instance = HttpRequestManager::get_instance();

            clear_rate_limit(type);

            switch (type) {
            case RateLimitType::RL_GENERAL:
                m_request.general_rate_limit =
                    instance.create_rate_limit(requests_per_period, period_ms, sequential);
                m_owns_general_rate_limit = true;
                break;

            case RateLimitType::RL_SPECIFIC:
                m_request.specific_rate_limit =
                    instance.create_rate_limit(requests_per_period, period_ms, sequential);
                m_owns_specific_rate_limit = true;
                break;
            }
        }

        /// \brief Sets the rate limit based on requests per minute (RPM).
        /// \param requests_per_minute Maximum number of requests allowed per minute.
        /// \param type The type of rate limit (either general or specific).
        /// \param sequential When true, no other request sharing this limit may start until
        ///        the current request (including all its retries) has finished.
        void set_rate_limit_rpm(
                long requests_per_minute,
                RateLimitType type = RateLimitType::RL_GENERAL,
                bool sequential = false) {
            long period_ms = 60000; // 1 minute in milliseconds
            set_rate_limit(requests_per_minute, period_ms, type, sequential);
        }

        /// \brief Sets the rate limit based on requests per second (RPS).
        /// \param requests_per_second Maximum number of requests allowed per second.
        /// \param type The type of rate limit (either general or specific).
        /// \param sequential When true, no other request sharing this limit may start until
        ///        the current request (including all its retries) has finished.
        void set_rate_limit_rps(
                long requests_per_second,
                RateLimitType type = RateLimitType::RL_GENERAL,
                bool sequential = false) {
            long period_ms = 1000; // 1 second in milliseconds
            set_rate_limit(requests_per_second, period_ms, type, sequential);
        }

        /// \brief Sets the partition key for both rate limits (general and specific).
        /// \param key Partition key used to separate rate-limit state within the same limit ID.
        ///        Empty string means the default shared state.
        void set_rate_limit_key(const std::string& key) {
            m_request.general_rate_limit_key = key;
            m_request.specific_rate_limit_key = key;
        }

        /// \brief Sets the partition key for the general rate limit.
        /// \param key Partition key used to separate general rate-limit state.
        ///        Empty string means the default shared state.
        void set_general_rate_limit_key(const std::string& key) {
            m_request.general_rate_limit_key = key;
        }

        /// \brief Sets the partition key for the specific rate limit.
        /// \param key Partition key used to separate specific rate-limit state.
        ///        Empty string means the default shared state.
        void set_specific_rate_limit_key(const std::string& key) {
            m_request.specific_rate_limit_key = key;
        }

        /// \brief Sets independent partition keys for general and specific rate limits.
        /// \param general_key Partition key for the general rate limit.
        /// \param specific_key Partition key for the specific rate limit.
        void set_rate_limit_keys(
                const std::string& general_key,
                const std::string& specific_key) {
            m_request.general_rate_limit_key = general_key;
            m_request.specific_rate_limit_key = specific_key;
        }

        /// \brief Sets the Accept-Encoding header.
        /// \param identity Enable identity encoding.
        /// \param deflate Enable deflate encoding.
        /// \param gzip Enable gzip encoding.
        /// \param brotli Enable brotli encoding.
        void set_accept_encoding(
                bool identity = false,
                bool deflate = false,
                bool gzip = false,
                bool brotli = false) {
            m_request.set_accept_encoding(identity, deflate, gzip, brotli);
        }

        /// \brief Sets a custom Accept-Encoding header value.
        /// \param value The custom value for the Accept-Encoding header.
        void set_accept_encoding(const std::string& value) {
            m_request.accept_encoding = value;
        }

        /// \brief Sets the Accept-Language header value.
        /// \param value The language value to be sent with the Accept-Language header.
        void set_accept_language(const std::string& value) {
            set_header("Accept-Language", value);
        }

        /// \brief Sets the Content-Type header value.
        /// \param value The MIME type for the Content-Type header.
        void set_content_type(const std::string& value) {
            set_header("Content-Type", value);
        }

        /// \brief Sets the Origin header value.
        /// \param value The origin to be sent with the Origin header.
        void set_origin(const std::string& value) {
            set_header("Origin", value);
        }

        /// \brief Sets the Referer header value.
        /// \param value The referer URL to be sent with the Referer header.
        void set_referer(const std::string& value) {
            set_header("Referer", value);
        }

        /// \brief Sets the Do Not Track (DNT) header value.
        /// \param value If true, sets the DNT header to "1".
        void set_dnt(const bool value) {
            m_request.headers.erase("dnt");
            if (value) m_request.headers.emplace("dnt", "1");
        }

        /// \brief Configures whether to follow redirects automatically.
        /// \param value If true, enables following HTTP redirects.
        void set_follow_location(bool value) {
            m_request.follow_location = value;
        }

        /// \brief Configures whether to automatically set the Referer header on redirects.
        /// \param value If true, enables automatically setting the Referer header during redirects.
        void set_auto_referer(bool value) {
            m_request.auto_referer = value;
        }

        /// \brief Configures whether to use a tunneling proxy for HTTP requests.
        /// \param value If true, enables tunneling through the proxy server. Tunneling proxies are typically used for HTTPS requests to securely forward traffic.
        void set_proxy_tunnel(bool value) {
            m_request.proxy_tunnel = value;
        }

        /// \brief Configures whether to send only the HTTP headers (HEAD request).
        /// \param value If true, the request will not download the response body (uses CURLOPT_NOBODY internally).
        /// Useful for measuring latency or checking resource availability without downloading content.
        void set_head_only(bool value) {
            m_request.head_only = value;
        }

        /// \brief Enables or disables intermediate callbacks for response body chunks.
        /// \param streaming Enable (true) or disable (false) streaming callbacks.
        void set_streaming(bool streaming) {
            m_request.streaming = streaming;
        }

        /// \brief Sets the proxy server address.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        /// \param type The type of proxy, defaulting to HTTP.
        void set_proxy(
                    const std::string& ip,
                    int port,
                    ProxyType type = ProxyType::PROXY_HTTP) {
            m_request.set_proxy(ip, port, type);
        }

        /// \brief Sets the proxy server address with authentication details.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        /// \param username Proxy username.
        /// \param password Proxy password.
        /// \param type The type of proxy, defaulting to HTTP.
        void set_proxy(
                const std::string& ip,
                const int port,
                const std::string& username,
                const std::string& password,
                ProxyType type = ProxyType::PROXY_HTTP) {
            m_request.set_proxy(ip, port, username, password, type);
        }

        /// \brief Sets proxy authentication credentials.
        /// \param username Proxy username.
        /// \param password Proxy password.
        void set_proxy_auth(
                const std::string& username,
                const std::string& password) {
            m_request.set_proxy_auth(username, password);
        }

        /// \brief Sets the proxy server address.
        /// \param server Proxy address in <ip:port> format.
        void set_proxy_server(const std::string& server) {
            m_request.set_proxy_server(server);
        }

        /// \brief Sets the proxy authentication credentials.
        /// \param auth Proxy authentication in <username:password> format.
        void set_proxy_auth(const std::string& auth) {
            m_request.set_proxy_auth(auth);
        }

        /// \brief Sets the proxy type.
        /// \param type Type of proxy.
        void set_proxy_type(ProxyType type) {
            m_request.set_proxy_type(type);
        }

        /// \brief Sets retry attempts and delay between retries for HTTP requests.
        /// \param retry_attempts Number of retry attempts.
        /// \param retry_delay_ms Delay in milliseconds between retry attempts.
        void set_retry_attempts(long retry_attempts, long retry_delay_ms) {
            m_request.set_retry_attempts(retry_attempts, retry_delay_ms);
        }

        /// \brief Adds a valid HTTP status code to the request.
        /// \param status The HTTP status code to allow.
        void add_valid_status(long status) {
            m_request.add_valid_status(status);
        }

        /// \brief Replaces all valid HTTP status codes for the request.
        /// \param statuses The set of HTTP status codes to allow.
        void set_valid_statuses(const std::set<long>& statuses) {
            m_request.set_valid_statuses(statuses);
        }

        /// \brief Clears the set of valid HTTP status codes for the request.
        void clear_valid_statuses() {
            m_request.clear_valid_statuses();
        }

        /// \brief Sets the User-Agent header.
        /// \param user_agent User-Agent string.
        void set_user_agent(const std::string& user_agent) {
            m_request.set_user_agent(user_agent);
        }

        /// \brief Sets the cookie string for HTTP requests.
        /// \param cookie Cookie data as a single string.
        void set_cookie(const std::string& cookie) {
            m_request.set_cookie(cookie);
        }

        /// \brief Sets the client certificate file path.
        /// \param cert_file Path to the client certificate file.
        void set_cert_file(const std::string& cert_file) {
            m_request.set_cert_file(cert_file);
        }

        /// \brief Sets the path to the CA certificate file.
        /// \param ca_file Path to the CA certificate file.
        void set_ca_file(const std::string& ca_file) {
            m_request.set_ca_file(ca_file);
        }

        /// \brief Sets the timeout duration for HTTP requests.
        /// \param timeout Timeout duration in seconds.
        void set_timeout(long timeout) {
            m_request.set_timeout(timeout);
        }

        /// \brief Sets the connection timeout duration.
        /// \param connect_timeout Connection timeout in seconds.
        void set_connect_timeout(long connect_timeout) {
            m_request.set_connect_timeout(connect_timeout);
        }

        /// \brief Enables or disables verbose output.
        /// \param verbose Enable (true) or disable (false) verbose output.
        void set_verbose(bool verbose) {
            m_request.verbose = verbose;
        }

        /// \brief Enables or disables debug headers.
        /// \param debug_header Enable (true) or disable (false) debug headers.
        void set_debug_header(bool debug_header) {
            m_request.debug_header = debug_header;
        }

        /// \brief Sets the maximum number of redirects for the client.
        /// \param max_redirects The maximum number of redirects allowed.
        void set_max_redirects(long max_redirects) {
            m_request.max_redirects = max_redirects;
        }

        /// \brief Attempts to submit a prepared request to the global HTTP manager.
        /// \param request_ptr Prepared HTTP request to be enqueued.
        /// \param callback Callback function to be called when the request is completed.
        /// \return SubmitResult describing whether the request was accepted into the queue.
        /// Returns `ClientError::QueueLimitExceeded` when this client's max-in-flight
        /// admission cap is reached.
        SubmitResult submit_request(
                std::unique_ptr<HttpRequest> request_ptr,
                HttpResponseCallback callback) {
            SubmitResult submit_result;
            {
                std::lock_guard<std::mutex> lock(m_submit_mutex);

                if (m_max_in_flight != 0 &&
                    HttpRequestManager::get_instance().group_request_count(m_request.group_id) >= m_max_in_flight) {
                    return SubmitResult{false, utils::make_error_code(utils::ClientError::QueueLimitExceeded)};
                }

                submit_result = HttpRequestManager::get_instance().submit_request(
                    std::move(request_ptr), std::move(callback));
            }

            if (submit_result) {
                core::NetworkWorker::get_instance().notify();
            }
            return submit_result;
        }

        /// \brief Sends an HTTP request with the specified method, path, and parameters.
        /// \param method The HTTP method (e.g., "GET", "POST").
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was accepted into the queue; false if admission was rejected.
        bool request(
                const std::string &method,
                const std::string& path,
                const QueryParams &query,
                const Headers &headers,
                const std::string &content,
                HttpResponseCallback callback) {
            return request(make_request(method, path, query, headers, content), std::move(callback));
        }

        /// \brief Sends an HTTP request with a temporary specific rate limit found by ID.
        /// \param method The HTTP method (e.g., "GET", "POST").
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \param specific_rate_limit_id ID of a registered rate limit.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was accepted into the queue; false if admission was rejected.
        /// \note If the ID is not found, the request is submitted without an additional specific limit.
        bool request(
                const std::string &method,
                const std::string& path,
                const QueryParams &query,
                const Headers &headers,
                const std::string &content,
                long specific_rate_limit_id,
                HttpResponseCallback callback) {
            return request(
                method,
                path,
                query,
                headers,
                content,
                HttpRequestManager::get_instance().get_rate_limit(specific_rate_limit_id),
                std::move(callback));
        }

        /// \brief Sends an HTTP request with a temporary specific rate limit.
        /// \param method HTTP method, e.g. "GET" or "POST".
        /// \param path URL path for the request.
        /// \param query Query parameters.
        /// \param headers Additional HTTP headers.
        /// \param content Request body content.
        /// \param specific_rate_limit Specific rate-limit handle applied only to this request.
        /// \param callback Callback invoked when the request completes.
        /// \return True if the request was accepted into the queue.
        bool request(
                const std::string &method,
                const std::string& path,
                const QueryParams &query,
                const Headers &headers,
                const std::string &content,
                const HttpRateLimitHandlePtr& specific_rate_limit,
                HttpResponseCallback callback) {
            auto request_ptr = make_request(method, path, query, headers, content);
            request_ptr->specific_rate_limit = specific_rate_limit;
            return request(std::move(request_ptr), std::move(callback));
        }

        /// \brief Sends a GET request.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was successfully added to the RequestManager; false otherwise.
        bool get(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                HttpResponseCallback callback) {
            return request("GET", path, query, headers, std::string(), std::move(callback));
        }

        /// \brief Sends a POST request.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was successfully added to the RequestManager; false otherwise.
        bool post(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                HttpResponseCallback callback) {
            return request("POST", path, query, headers, content, std::move(callback));
        }

        /// \brief Sends a GET request with a temporary specific rate limit found by ID.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param specific_rate_limit_id ID of a registered rate limit.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was successfully added to the RequestManager; false otherwise.
        /// \note If the ID is not found, the request is submitted without an additional specific limit.
        bool get(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                long specific_rate_limit_id,
                HttpResponseCallback callback) {
            return request("GET", path, query, headers, std::string(), specific_rate_limit_id, std::move(callback));
        }

        /// \brief Sends a GET request with a temporary specific rate limit.
        /// \param path URL path for the request.
        /// \param query Query parameters.
        /// \param headers Additional HTTP headers.
        /// \param specific_rate_limit Specific rate-limit handle applied only to this request.
        /// \param callback Callback invoked when the request completes.
        /// \return True if the request was accepted into the queue.
        bool get(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const HttpRateLimitHandlePtr& specific_rate_limit,
                HttpResponseCallback callback) {
            return request("GET", path, query, headers, std::string(), specific_rate_limit, std::move(callback));
        }

        /// \brief Sends a POST request with a temporary specific rate limit found by ID.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \param specific_rate_limit_id ID of a registered rate limit.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was successfully added to the RequestManager; false otherwise.
        /// \note If the ID is not found, the request is submitted without an additional specific limit.
        bool post(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                long specific_rate_limit_id,
                HttpResponseCallback callback) {
            return request("POST", path, query, headers, content, specific_rate_limit_id, std::move(callback));
        }

        /// \brief Sends a POST request with a temporary specific rate limit.
        /// \param path URL path for the request.
        /// \param query Query parameters.
        /// \param headers Additional HTTP headers.
        /// \param content Request body content.
        /// \param specific_rate_limit Specific rate-limit handle applied only to this request.
        /// \param callback Callback invoked when the request completes.
        /// \return True if the request was accepted into the queue.
        bool post(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                const HttpRateLimitHandlePtr& specific_rate_limit,
                HttpResponseCallback callback) {
            return request("POST", path, query, headers, content, specific_rate_limit, std::move(callback));
        }

        /// \brief Sends an HTTP request with a specified method, path, and parameters, and returns a future with the response.
        /// \param method The HTTP method (e.g., "GET", "POST").
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \return A future containing the HttpResponsePtr object.
        std::future<HttpResponsePtr> request(
                const std::string& method,
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content) {
            return submit_future_request(make_request(method, path, query, headers, content));
        }

        /// \brief Sends an HTTP request with a temporary specific rate limit found by ID and returns a future with the response.
        /// \param method The HTTP method (e.g., "GET", "POST").
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \param specific_rate_limit_id ID of a registered rate limit.
        /// \return A future containing the HttpResponsePtr object.
        /// \note If the ID is not found, the request is submitted without an additional specific limit.
        std::future<HttpResponsePtr> request(
                const std::string& method,
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                long specific_rate_limit_id) {
            return request(
                method,
                path,
                query,
                headers,
                content,
                HttpRequestManager::get_instance().get_rate_limit(specific_rate_limit_id));
        }

        /// \brief Sends an HTTP request with a temporary specific rate limit and returns a future with the response.
        /// \param method HTTP method, e.g. "GET" or "POST".
        /// \param path URL path for the request.
        /// \param query Query parameters.
        /// \param headers Additional HTTP headers.
        /// \param content Request body content.
        /// \param specific_rate_limit Specific rate-limit handle applied only to this request.
        /// \return A future containing the HttpResponsePtr object.
        std::future<HttpResponsePtr> request(
                const std::string& method,
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                const HttpRateLimitHandlePtr& specific_rate_limit) {
            auto request_ptr = make_request(method, path, query, headers, content);
            request_ptr->specific_rate_limit = specific_rate_limit;
            return submit_future_request(std::move(request_ptr));
        }

        /// \brief Sends a GET request asynchronously and returns a future with the response.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \return A future containing the HttpResponsePtr object.
        std::future<HttpResponsePtr> get(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers) {
            return request("GET", path, query, headers, std::string());
        }

        /// \brief Sends a POST request asynchronously and returns a future with the response.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \return A future containing the HttpResponsePtr object.
        std::future<HttpResponsePtr> post(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content) {
            return request("POST", path, query, headers, content);
        }

        /// \brief Sends an asynchronous GET request with a temporary specific rate limit found by ID.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param specific_rate_limit_id ID of a registered rate limit.
        /// \return A future containing the HttpResponsePtr object.
        /// \note If the ID is not found, the request is submitted without an additional specific limit.
        std::future<HttpResponsePtr> get(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                long specific_rate_limit_id) {
            return request("GET", path, query, headers, std::string(), specific_rate_limit_id);
        }

        /// \brief Sends an asynchronous POST request with a temporary specific rate limit found by ID.
        /// \param path The URL path for the request.
        /// \param query The query arguments.
        /// \param headers The HTTP headers.
        /// \param content The request body content.
        /// \param specific_rate_limit_id ID of a registered rate limit.
        /// \return A future containing the HttpResponsePtr object.
        /// \note If the ID is not found, the request is submitted without an additional specific limit.
        std::future<HttpResponsePtr> post(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                long specific_rate_limit_id) {
            return request("POST", path, query, headers, content, specific_rate_limit_id);
        }

        /// \brief Sends an asynchronous GET request with a temporary specific rate limit.
        /// \param path URL path for the request.
        /// \param query Query parameters.
        /// \param headers Additional HTTP headers.
        /// \param specific_rate_limit Specific rate-limit handle applied only to this request.
        /// \return A future containing the HttpResponsePtr object.
        std::future<HttpResponsePtr> get(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const HttpRateLimitHandlePtr& specific_rate_limit) {
            return request("GET", path, query, headers, std::string(), specific_rate_limit);
        }

        /// \brief Sends an asynchronous POST request with a temporary specific rate limit.
        /// \param path URL path for the request.
        /// \param query Query parameters.
        /// \param headers Additional HTTP headers.
        /// \param content Request body content.
        /// \param specific_rate_limit Specific rate-limit handle applied only to this request.
        /// \return A future containing the HttpResponsePtr object.
        std::future<HttpResponsePtr> post(
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content,
                const HttpRateLimitHandlePtr& specific_rate_limit) {
            return request("POST", path, query, headers, content, specific_rate_limit);
        }

    private:
        HttpRequest m_request;  ///< Request defaults shared by requests created by this client.
        std::string m_host;     ///< The base host URL for the HTTP client.
        bool m_owns_general_rate_limit = false; ///< Flag indicating if the client owns the general rate limit.
        bool m_owns_specific_rate_limit = false; ///< Flag indicating if the client owns the specific rate limit.
        mutable std::mutex m_submit_mutex; ///< Protects client-side submission settings and admission checks.
        std::size_t m_max_in_flight = 0; ///< Maximum number of in-flight requests for this client group, or 0 for disabled.
#       if KURLYK_AUTH_SUPPORT
        std::shared_ptr<http::auth::IAuthProvider> m_auth_provider; ///< Optional authentication provider applied to every request.
#       endif

        /// \brief Adds the request to the request manager and notifies the worker to process it.
        /// \param request_ptr The HTTP request to be sent.
        /// \param callback The callback function to be called when the request is completed.
        /// \return true if the request was successfully added to the RequestManager; false otherwise.
        bool request(
                std::unique_ptr<HttpRequest> request_ptr,
                HttpResponseCallback callback) {
            return submit_request(std::move(request_ptr), std::move(callback)).accepted;
        }

        void set_header(const std::string& name, const std::string& value) {
            m_request.headers.erase(name);
            m_request.headers.emplace(name, value);
        }

        std::unique_ptr<HttpRequest> make_request(
                const std::string& method,
                const std::string& path,
                const QueryParams& query,
                const Headers& headers,
                const std::string& content) const {
#           if __cplusplus >= 201402L
            auto request_ptr = std::make_unique<HttpRequest>(m_request);
#           else
            auto request_ptr = std::unique_ptr<HttpRequest>(new HttpRequest(m_request));
#           endif

            request_ptr->request_id = HttpRequestManager::get_instance().generate_request_id();
            request_ptr->method = method;
            request_ptr->set_url(m_host, path, query);
            request_ptr->headers.insert(headers.begin(), headers.end());
            request_ptr->content = content;
#           if KURLYK_AUTH_SUPPORT
            if (m_auth_provider) {
                m_auth_provider->authorize(*request_ptr);
            }
#           endif
            return request_ptr;
        }

        std::future<HttpResponsePtr> submit_future_request(std::unique_ptr<HttpRequest> request_ptr) {
            auto promise = std::make_shared<std::promise<HttpResponsePtr>>();
            auto future = promise->get_future();

            HttpResponseCallback callback = [promise](HttpResponsePtr response) {
                safe_set_response(promise, std::move(response));
            };

            safe_submit_request(promise, std::move(request_ptr), std::move(callback));
            return future;
        }

        /// \brief Creates a future that becomes ready when all requests in this client group finish.
        /// \return A future that is satisfied when the group becomes idle.
        std::future<void> make_wait_requests_future(uint64_t* waiter_id) {
            auto promise = std::make_shared<std::promise<void>>();
            auto future = promise->get_future();

            const uint64_t registered_waiter_id =
                HttpRequestManager::get_instance().wait_requests_by_group_id(
                m_request.group_id,
                [promise]() {
                    try {
                        promise->set_value();
                    } catch (const std::future_error& e) {
                        if (e.code() == std::make_error_condition(std::future_errc::promise_already_satisfied)) {
                            KURLYK_HANDLE_ERROR(e, "Promise already satisfied in HttpClient::wait_requests callback");
                        } else {
                            KURLYK_HANDLE_ERROR(e, "Future error in HttpClient::wait_requests callback");
                        }
                    } catch (const std::exception& e) {
                        KURLYK_HANDLE_ERROR(e, "Unhandled exception in HttpClient::wait_requests callback");
                    } catch (...) {
                        // Unknown fatal error in wait callback
                    }
                });
            if (waiter_id) {
                *waiter_id = registered_waiter_id;
            }

            return future;
        }

        /// \brief Safely sets the response value on the given promise.
        /// \param promise Promise that receives the HTTP response.
        /// \param response Completed HTTP response to forward to the caller.
        static void safe_set_response(
                std::shared_ptr<std::promise<HttpResponsePtr>> promise,
                HttpResponsePtr response) {
            if (!response || !response->ready) return;
            try {
                promise->set_value(std::move(response));
            } catch (const std::future_error& e) {
                if (e.code() == std::make_error_condition(std::future_errc::promise_already_satisfied)) {
                    KURLYK_HANDLE_ERROR(e, "Promise already satisfied in HttpClient::request callback");
                } else {
                    KURLYK_HANDLE_ERROR(e, "Future error in HttpClient::request callback");
                }
            } catch (const std::exception& e) {
                KURLYK_HANDLE_ERROR(e, "Unhandled exception in HttpClient::request callback");
            } catch (...) {
                // Unknown fatal error in request callback
            }
        }

        /// \brief Creates a ready HTTP response describing a synchronous submission rejection.
        /// \param submit_result Submission result containing the rejection error code.
        /// \return A ready HttpResponsePtr describing the rejection.
        static HttpResponsePtr make_submit_error_response(const SubmitResult& submit_result) {
#           if __cplusplus >= 201402L
            auto response = std::make_unique<HttpResponse>();
#           else
            auto response = std::unique_ptr<HttpResponse>(new HttpResponse());
#           endif
            response->ready = true;
            response->status_code = 0;
            response->error_code = submit_result.error_code;
            response->error_message = submit_result.error_code.message();
            return response;
        }

        /// \brief Submits a request and propagates any failure to the provided promise.
        /// \param promise Promise to signal upon success or failure.
        /// \param request_ptr Prepared HTTP request to enqueue.
        /// \param callback Callback executed when the request completes.
        void safe_submit_request(
                std::shared_ptr<std::promise<HttpResponsePtr>> promise,
                std::unique_ptr<HttpRequest> request_ptr,
                HttpResponseCallback callback) {
            try {
                const SubmitResult submit_result = submit_request(std::move(request_ptr), std::move(callback));
                if (!submit_result) {
                    safe_set_response(promise, make_submit_error_response(submit_result));
                }
            } catch (const std::exception& e) {
                KURLYK_HANDLE_ERROR(e, "Exception while submitting request");
                try {
                    promise->set_exception(std::current_exception());
                } catch (...) {} // fallback
            } catch (...) {
                // Unknown fatal error while submitting request
                try {
                    promise->set_exception(std::current_exception());
                } catch (...) {} // fallback
            }
        }

        /// \brief Ensures that the network worker and request manager are initialized.
        static void ensure_initialized() {
            static std::once_flag once;
            std::call_once(once, []() {
                HttpRequestManager::get_instance();
                core::NetworkWorker::get_instance().start(KURLYK_AUTO_INIT_USE_ASYNC);
            });
        }

    }; // HttpClient

}; // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_HTTP_CLIENT_HPP_INCLUDED
