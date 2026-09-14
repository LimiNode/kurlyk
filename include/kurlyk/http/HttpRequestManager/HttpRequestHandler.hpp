#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_REQUEST_HANDLER_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_REQUEST_HANDLER_HPP_INCLUDED

/// \file RequestHandler.hpp
/// \brief Defines the HttpRequestHandler class for managing asynchronous HTTP requests.

namespace kurlyk {

    /// \class HttpRequestHandler
    /// \brief Manages asynchronous HTTP requests, including handling responses, retries, and error processing.
    class HttpRequestHandler {
    public:

        /// \brief Constructs an HttpRequestHandler with the specified request context.
        /// \param context Unique pointer to the HttpRequestContext object.
        explicit HttpRequestHandler(std::unique_ptr<HttpRequestContext> context)
            : m_request_context(std::move(context)) {
            std::fill(m_error_buffer, m_error_buffer + CURL_ERROR_SIZE, '\0');
#           if __cplusplus >= 201402L
            m_response = std::make_unique<HttpResponse>();
#           else
            m_response = std::unique_ptr<HttpResponse>(new HttpResponse());
#           endif
            init_curl();
        }

        /// \brief Destructor for HttpRequestHandler, handling cleanup of CURL and headers.
        ///
        /// If the callback has not been called yet, this indicates the request was incomplete,
        /// and an error response is passed to the callback.
        ~HttpRequestHandler() {
            if (m_curl) {
                curl_easy_cleanup(m_curl);
                curl_slist_free_all(m_headers);
            }
            if (!m_callback_called && m_response && m_request_context) {
                m_response->error_code = utils::make_error_code(utils::ClientError::AbortedDuringDestruction);
                m_response->status_code = 499; // Client closed request
                m_response->ready = true;
                m_request_context->callback(std::move(m_response));
                m_request_context->complete();
            }
        }

        /// \brief Processes body data received from server and appends it to response content.
        static size_t write_http_response_body(char* data, size_t size, size_t nmemb, void* userdata) {
            size_t total_size = size * nmemb;
            auto* handler = static_cast<HttpRequestHandler*>(userdata);
            if (handler) return handler->write_response_body(data, total_size);
            return total_size;
        }

        /// \brief Parses and stores response headers in the Headers container.
        static size_t parse_http_response_header(char* buffer, size_t size, size_t nitems, void* userdata) {
            size_t buffer_size = size * nitems;
            auto* headers = static_cast<Headers*>(userdata);
            std::string key, value;
            utils::parse_http_header_pair(buffer, buffer_size, key, value);
            if (!key.empty()) {
                headers->emplace(key, value);
            }
            return buffer_size;
        }

        /// \brief Processes a CURL message and determines if a callback should be invoked.
        bool handle_curl_message(CURLMsg* message) {
            if (!m_response) return true;

            m_response->error_message = std::string(m_error_buffer, std::strlen(m_error_buffer));
            // m_response->error_code = utils::make_error_code(message->data.result);
            curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &m_response->status_code);

            // If a timeout occurred, override the HTTP response code.
            if (message->data.result == CURLE_OPERATION_TIMEDOUT) {
                m_response->status_code = 499; // Client Closed Request
            }

            if (m_response->status_code == 0 &&
                message->data.result != CURLE_OK) {
                m_response->status_code = 451; // Unavailable For Legal Reasons
            }

            if (message->data.result != CURLE_OK) {
                m_response->error_code = utils::make_error_code(message->data.result);
            } else 
            if (m_response->status_code >= 400) {
                m_response->error_code = utils::make_http_error(m_response->status_code);
            } else {
                m_response->error_code = {};
            }

            const auto& valid_statuses = m_request_context->request->valid_statuses;
            long retry_attempts = m_request_context->request->retry_attempts;
            long& retry_attempt = m_request_context->retry_attempt;
            ++retry_attempt;

            m_response->retry_attempt = retry_attempt;
            const bool has_curl_error = message->data.result != CURLE_OK;
            const bool has_valid_status = valid_statuses.count(m_response->status_code) > 0;
            const bool should_retry =
                retry_attempts &&
                (has_curl_error || !has_valid_status) &&
                retry_attempt < retry_attempts &&
                !m_has_stream_chunk;

            if (!should_retry) {
                fill_response_timings();
                m_response->ready = true;
                m_request_context->callback(std::move(m_response));
                m_callback_called = true;
                m_done = true;
                if (m_request_context) m_request_context->complete();
                return true;
            }
            m_request_context->start_time = std::chrono::steady_clock::now();
            m_request_context->callback(std::move(m_response));
            m_callback_called = true;
            return false;
        }

        /// \brief Retrieves the CURL handle associated with this request.
        /// \return A pointer to the CURL handle used for this request, or nullptr if not initialized.
        CURL* get_curl() noexcept { return m_curl; }

        /// \brief Returns the unique pointer to the HttpRequestContext object.
        /// \return A unique pointer to the HttpRequestContext object associated with this request.
        std::unique_ptr<HttpRequestContext> get_request_context() { return std::move(m_request_context); }

        /// \brief Retrieves the unique ID of the HTTP request.
        /// \return The unique ID of the HTTP request if the context exists, or 0 if no context is set.
        uint64_t get_request_id() { return m_request_context ? m_request_context->request->request_id : 0; }

        /// \brief Retrieves the group ID of the HTTP request.
        /// \return The group ID of the HTTP request if the context exists, or 0 if no context is set.
        uint64_t get_group_id() { return m_request_context ? m_request_context->request->group_id : 0; }

        /// \brief Checks whether this request has already received its final callback.
        /// \return True if the request is fully completed (final callback or cancel).
        bool is_done() const noexcept { return m_done; }

        /// \brief Marks the request as done without invoking a callback.
        /// Used when the handler stays in the batch after curl removal (retry extraction).
        void mark_done() noexcept { m_done = true; }

        /// \brief Marks the request as cancelled.
        void cancel() {
            if (!m_callback_called) {
                m_response->error_code = utils::make_error_code(utils::ClientError::CancelledByUser);
                m_response->status_code = 499; // Client closed request
                m_response->ready = true;
                if (m_request_context) {
                    m_request_context->callback(std::move(m_response));
                }
                m_callback_called = true;
                m_done = true;
                if (m_request_context) m_request_context->complete();
            }
        }

    private:
        std::unique_ptr<HttpRequestContext> m_request_context;  ///< Context for the current request.
        std::unique_ptr<HttpResponse>       m_response;         ///< Response object.
        CURL*                               m_curl = nullptr;   ///< CURL handle for the request.
        struct curl_slist*                  m_headers = nullptr; ///< CURL headers list.
        char                                m_error_buffer[CURL_ERROR_SIZE]; ///< Buffer for CURL error messages.
        bool                                m_callback_called = false; ///< Indicates if the callback was called.
        bool                                m_has_stream_chunk = false; ///< Indicates if a streaming body chunk was emitted.
        bool                                m_done = false; ///< Indicates if the request has received its final callback.
        mutable std::string                 m_ca_file; ///< Cached CA file path.

        /// \brief Initializes CURL options for the request, setting headers, method, SSL, timeouts, and other parameters.
        void init_curl() {
            if (!m_request_context) return;
            const auto& request = m_request_context->request;
            m_curl = curl_easy_init();
            if (!m_curl) return;

            curl_easy_setopt(m_curl, CURLOPT_URL, request->url.c_str());
            curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L); // Disable signals for thread safety.
            curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, request->method.c_str());
            if (request->head_only) {
                curl_easy_setopt(m_curl, CURLOPT_NOBODY, 1L);
            }
            curl_easy_setopt(m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

            set_ssl_options(*request);
            set_request_options(*request);

            curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, m_error_buffer);
            curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, write_http_response_body);
            curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, &m_response->headers);
            curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, parse_http_response_header);
            curl_easy_setopt(m_curl, CURLOPT_PRIVATE, this);
        }

        /// \brief Stores received body data and emits a streaming chunk when enabled.
        size_t write_response_body(const char* data, size_t total_size) {
            if (m_response) {
                m_response->content.append(data, total_size);
            }
            emit_stream_chunk(data, total_size);
            return total_size;
        }

        /// \brief Invokes the response callback with an intermediate body chunk.
        void emit_stream_chunk(const char* data, size_t total_size) {
            if (!data || total_size == 0 || !m_request_context ||
                !m_request_context->request ||
                !m_request_context->request->streaming ||
                !m_request_context->callback) {
                return;
            }

#           if __cplusplus >= 201402L
            auto chunk = std::make_unique<HttpResponse>();
#           else
            auto chunk = std::unique_ptr<HttpResponse>(new HttpResponse());
#           endif

            if (m_response) {
                chunk->headers = m_response->headers;
            }
            chunk->content.assign(data, total_size);
            chunk->retry_attempt = m_request_context->retry_attempt + 1;
            chunk->ready = false;
            chunk->stream_chunk = true;
            if (m_curl) {
                curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &chunk->status_code);
            }

            m_has_stream_chunk = true;

            try {
                m_request_context->callback(std::move(chunk));
            } catch (const std::exception& e) {
                KURLYK_HANDLE_ERROR(e, "Unhandled exception in HttpRequestHandler streaming callback");
            } catch (...) {
                // Unknown fatal error in streaming callback
            }
        }
        
        void fill_response_timings() {
            curl_easy_getinfo(m_curl, CURLINFO_NAMELOOKUP_TIME,    &m_response->namelookup_time);
            curl_easy_getinfo(m_curl, CURLINFO_CONNECT_TIME,       &m_response->connect_time);
            curl_easy_getinfo(m_curl, CURLINFO_APPCONNECT_TIME,    &m_response->appconnect_time);
            curl_easy_getinfo(m_curl, CURLINFO_PRETRANSFER_TIME,   &m_response->pretransfer_time);
            curl_easy_getinfo(m_curl, CURLINFO_STARTTRANSFER_TIME, &m_response->starttransfer_time);
            curl_easy_getinfo(m_curl, CURLINFO_TOTAL_TIME,         &m_response->total_time);
        }

        /// \brief Sets SSL options such as cert, key, and CA file.
        void set_ssl_options(const HttpRequest& request) {
            if (!request.cert_file.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_SSLCERT, request.cert_file.c_str());
            }
            if (!request.key_file.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_SSLKEY, request.key_file.c_str());
            }
            if (!request.ca_file.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_CAINFO, request.ca_file.c_str());
            }
#           if defined(_WIN32)
            else {
                curl_easy_setopt(m_curl, CURLOPT_CAINFO, get_ca_file_path());
            }
#           endif
            if (!request.ca_path.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_CAPATH, request.ca_path.c_str());
            }
            curl_easy_setopt(m_curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_MAX_DEFAULT);
        }

        /// \brief Sets general options such as headers, cookies, and proxy.
        void set_request_options(const HttpRequest& request) {
            curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, request.follow_location);
            curl_easy_setopt(m_curl, CURLOPT_MAXREDIRS, request.max_redirects);
            curl_easy_setopt(m_curl, CURLOPT_AUTOREFERER, request.auto_referer);
            curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, request.timeout);
            curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, request.connect_timeout);

            set_custom_headers(request);
            set_proxy_options(request);
            set_cookie_options(request);
            set_request_body(request);
            if (request.use_interface && request.proxy_server.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_INTERFACE, request.interface_name.c_str());
            }
            curl_easy_setopt(m_curl, CURLOPT_VERBOSE, request.verbose);
            curl_easy_setopt(m_curl, CURLOPT_HEADER, request.debug_header);
        }

        /// \brief Appends custom headers to the request if provided.
        void set_custom_headers(const HttpRequest& request) {
            if (!request.user_agent.empty() && request.headers.count("User-Agent") == 0) {
                curl_easy_setopt(m_curl, CURLOPT_USERAGENT, request.user_agent.c_str());
            }
            if (!request.accept_encoding.empty() && request.headers.count("Accept-Encoding") == 0) {
                curl_easy_setopt(m_curl, CURLOPT_ACCEPT_ENCODING, request.accept_encoding.c_str());
            }

            for (const auto& header : request.headers) {
                std::string header_line = header.first + ": " + header.second;
                m_headers = curl_slist_append(m_headers, header_line.c_str());
                if (!m_headers) {
                    curl_slist_free_all(m_headers);
                    break;
                }
            }
            if (m_headers) {
                curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_headers);
            }
        }

        /// \brief Configures proxy options if set in the request.
        void set_proxy_options(const HttpRequest& request) {
            if (!request.proxy_server.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_PROXY, request.proxy_server.c_str());
                curl_easy_setopt(m_curl, CURLOPT_PROXYTYPE, to_curl_proxy_type(request.proxy_type));
                curl_easy_setopt(m_curl, CURLOPT_HTTPPROXYTUNNEL, request.proxy_tunnel);
                if (!request.proxy_auth.empty()) {
                    curl_easy_setopt(m_curl, CURLOPT_PROXYUSERPWD, request.proxy_auth.c_str());
                    curl_easy_setopt(m_curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
                }
            }
        }

        /// \brief Sets cookie options if cookies are specified in the request.
        void set_cookie_options(const HttpRequest& request) {
            if (!request.cookie.empty() && request.headers.count("Cookie") == 0) {
                curl_easy_setopt(m_curl, CURLOPT_COOKIE, request.cookie.c_str());
            } else if (!request.cookie_file.empty()) {
                curl_easy_setopt(m_curl, CURLOPT_COOKIEFILE, request.cookie_file.c_str());
                curl_easy_setopt(m_curl, CURLOPT_COOKIEJAR, request.cookie_file.c_str());
                if (request.clear_cookie_file) {
                    curl_easy_setopt(m_curl, CURLOPT_COOKIELIST, "ALL");
                }
            }
        }

        /// \brief Sets request body content for applicable HTTP methods.
        void set_request_body(const HttpRequest& request) {
            if (request.head_only) return;
            if (utils::case_insensitive_equal(request.method, "POST") ||
                utils::case_insensitive_equal(request.method, "PUT") ||
                utils::case_insensitive_equal(request.method, "PATCH") ||
                utils::case_insensitive_equal(request.method, "DELETE")) {
                curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, request.content.c_str());
                curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.content.size()));
            }
        }

        /// \brief Gets the full path to the CA certificate file.
        const char* get_ca_file_path() const {
#           if defined(_WIN32)
            m_ca_file = utils::get_exec_dir() + "\\curl-ca-bundle.crt";
            m_ca_file = utils::utf8_to_ansi(m_ca_file);
#           else
            m_ca_file = utils::get_exec_dir() + "/curl-ca-bundle.crt";
#           endif
            return m_ca_file.c_str();
        }
    }; // HttpRequestHandler

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_HTTP_REQUEST_MANAGER_HTTP_REQUEST_HANDLER_HPP_INCLUDED
