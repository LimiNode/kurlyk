#pragma once
#ifndef KURLYK_HEADER_KURLYK_HTTP_DATA_HTTP_REQUEST_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_HTTP_DATA_HTTP_REQUEST_HPP_INCLUDED

/// \file HttpRequest.hpp
/// \brief Defines the HttpRequest class for configuring HTTP requests.

namespace kurlyk {

    /// \class HttpRequest
    /// \brief Represents an HTTP request configuration.
    ///
    /// HttpRequest stores parameters for an HTTP request,
    /// including headers, URL, method, body, proxy, TLS, timeout, retry,
    /// and rate-limit options.
    class HttpRequest {
    public:
        uint64_t request_id = 0;         ///< Unique ID of this concrete HTTP request.
        uint64_t group_id = 0;           ///< ID shared by related requests, for example all requests created by one HttpClient.
        Headers headers;                 ///< HTTP request headers.
        std::string url;                 ///< Full request URL.
        std::string method = "GET";      ///< HTTP request method (e.g., "GET", "POST").
        std::string content;             ///< Data payload for the request.
        std::string user_agent;          ///< User-Agent header.
        std::string accept_encoding;     ///< Accept-Encoding header.
        std::string cookie_file;         ///< Path to the cookie file; if empty, cookies are not saved.
        std::string cookie;              ///< Cookie data as a string.
        std::string cert_file;           ///< Path to the client certificate file.
        std::string key_file;            ///< Path to the private key for the client certificate.
        std::string ca_file;             ///< Path to the CA certificate file.
        std::string ca_path;             ///< Path to a directory containing CA certificates.
        std::string proxy_server;        ///< Proxy address in <ip:port> format.
        std::string proxy_auth;          ///< Proxy authentication in <username:password> format.
        ProxyType proxy_type = ProxyType::PROXY_HTTP; ///< Proxy type (e.g., HTTP, SOCKS5).
        bool proxy_tunnel  = true;       ///< Enable proxy tunneling.
        std::string interface_name;      ///< Network interface name to use for the request.
        bool use_interface = false;      ///< Enable the specified network interface.

        bool follow_location = true;     ///< Automatically follow HTTP redirects.
        long max_redirects   = 10;       ///< Maximum allowed redirects.
        bool auto_referer    = false;    ///< Automatically set Referer header.
        bool head_only       = false;    ///< If `true`, does not download the response body (HEAD-like behavior).
        bool streaming       = false;    ///< Enable intermediate callbacks for response body chunks.

        /// \brief Maximum response body bytes accepted per attempt; zero preserves unlimited downloads.
        /// \details Counts actual bytes passed by libcurl after transfer/content decoding, not Content-Length.
        /// A chunk exceeding the remaining budget is neither stored nor emitted. The accepted prefix may
        /// remain in the final response, which completes with ResponseTooLarge and is never retried.
        /// Diagnostic headers also count when debug_header routes them through the body callback.
        std::size_t max_response_body_bytes = 0;

        long timeout         = 30;       ///< Request timeout in seconds.
        long connect_timeout = 10;       ///< Connection timeout in seconds.
        HttpRateLimitHandlePtr general_rate_limit;  ///< General rate limit handle.
        HttpRateLimitHandlePtr specific_rate_limit; ///< Specific rate limit handle.
        std::string general_rate_limit_key;         ///< Key used to separate general rate limit state; empty means default shared state.
        std::string specific_rate_limit_key;        ///< Key used to separate specific rate limit state; empty means default shared state.
        std::set<long> valid_statuses = {200}; ///< Set of valid HTTP response status codes.
        long retry_attempts = 0;         ///< Number of retry attempts in case of failure.
        long retry_delay_ms = 0;         ///< Delay between retry attempts in milliseconds.

        bool clear_cookie_file = false;  ///< Flag to clear the cookie file at the start of the request.

        // Debug parameters
        bool verbose = false;            ///< Enable verbose output (CURLOPT_VERBOSE).
        bool debug_header = false;       ///< Include headers in debug output (CURLOPT_HEADER).

        /// \brief Sets the request URL with base URL, path, and optional query parameters.
        /// \param host Base URL or host prefix, for example "https://example.com".
        /// \param path Path to resource.
        /// \param query Optional query parameters as a string.
        void set_url(
                const std::string& host,
                const std::string& path,
                const std::string& query = std::string()) {
            url = host;
            if (!path.empty() && path[0] != '/') {
                url += "/";
            }
            url += path;
            if (!query.empty()) {
                if (query[0] != '?') url += "?";
                url += query;
            }
        }

        /// \brief Sets the request URL with base URL, path, and query parameters.
        /// \param host Base URL or host prefix, for example "https://example.com".
        /// \param path Path to resource.
        /// \param query Query parameters in dictionary format.
        void set_url(
                const std::string& host,
                const std::string& path,
                const QueryParams& query) {
            const std::string query_str = utils::to_query_string(query, "?");
            set_url(host, path, query_str);
        }

        /// \brief Sets the request URL and appends optional query parameters.
        /// \param url Full request URL.
        /// \param query Query parameters in dictionary format.
        void set_url(const std::string& url, const QueryParams& query) {
            const std::string args_str = utils::to_query_string(query, "?");
            this->url = url;
            if (!query.empty()) {
                this->url += args_str;
            }
        }

        /// \brief Sets the Accept-Encoding header with optional encoding types.
        /// \param identity Enable "identity" encoding.
        /// \param deflate Enable "deflate" encoding.
        /// \param gzip Enable "gzip" encoding.
        /// \param brotli Enable "br" (brotli) encoding.
        void set_accept_encoding(
                bool identity = false,
                bool deflate = false,
                bool gzip = false,
                bool brotli = false) {
            std::string encodings;
            if (identity) encodings += "identity";
            if (deflate) encodings += (encodings.empty() ? "" : ",") + std::string("deflate");
            if (gzip) encodings += (encodings.empty() ? "" : ",") + std::string("gzip");
            if (brotli) encodings += (encodings.empty() ? "" : ",") + std::string("br");

            accept_encoding = std::move(encodings);
        }

        /// \brief Sets the Accept-Language header value.
        /// \param value Language value to be sent with the Accept-Language header.
        void set_accept_language(const std::string& value) {
            headers.emplace("Accept-Language", value);
        }

        /// \brief Sets the Content-Type header value.
        /// \param value MIME type for the Content-Type header.
        void set_content_type(const std::string& value) {
            headers.emplace("Content-Type", value);
        }

        /// \brief Sets the Origin header value.
        /// \param value Origin to be sent with the Origin header.
        void set_origin(const std::string& value) {
            headers.emplace("Origin", value);
        }

        /// \brief Sets the Referer header value.
        /// \param value Referer URL to be sent with the Referer header.
        void set_referer(const std::string& value) {
            headers.emplace("Referer", value);
        }

        /// \brief Sets the proxy server address.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        void set_proxy(
                const std::string& ip,
                int port) {
            proxy_server = ip + ":" + std::to_string(port);
        }

        /// \brief Sets the proxy server address.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        /// \param type Proxy type.
        void set_proxy(
                const std::string& ip,
                int port,
                ProxyType type) {
            proxy_server = ip + ":" + std::to_string(port);
            proxy_type = type;
        }

        /// \brief Sets the proxy server address with authentication details.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        /// \param username Proxy username.
        /// \param password Proxy password.
        /// \param type Proxy type.
        void set_proxy(
                const std::string& ip,
                int port,
                const std::string& username,
                const std::string& password,
                ProxyType type = ProxyType::PROXY_HTTP) {
            set_proxy(ip, port);
            set_proxy_auth(username, password);
            proxy_type = type;
        }

        /// \brief Sets the proxy server address.
        /// \param server Proxy address in <ip:port> format.
        void set_proxy_server(const std::string& server) {
            proxy_server = server;
        }

        /// \brief Sets the proxy authentication credentials.
        /// \param auth Proxy authentication in <username:password> format.
        void set_proxy_auth(const std::string& auth) {
            proxy_auth = auth;
        }

        /// \brief Sets the proxy type.
        /// \param type Type of proxy.
        void set_proxy_type(ProxyType type) {
            proxy_type = type;
        }

        /// \brief Sets proxy authentication credentials.
        /// \param username Proxy username.
        /// \param password Proxy password.
        void set_proxy_auth(
                const std::string& username,
                const std::string& password) {
            proxy_auth = username + ":" + password;
        }

        /// \brief Sets the number of retry attempts and delay before retrying.
        /// \param retry_attempts Maximum number of retry attempts.
        /// \param retry_delay_ms Delay before retrying in milliseconds.
        void set_retry_attempts(long retry_attempts, long retry_delay_ms) {
            this->retry_attempts = retry_attempts;
            this->retry_delay_ms = retry_delay_ms;
        }
        
        /// \brief Adds a single valid HTTP status code.
        /// \param status HTTP status code to add to the set of valid statuses.
        void add_valid_status(long status) {
            valid_statuses.insert(status);
        }

        /// \brief Sets the valid HTTP status codes, replacing any existing values.
        /// \param statuses New set of valid HTTP status codes.
        void set_valid_statuses(const std::set<long>& statuses) {
            valid_statuses = statuses;
        }

        /// \brief Clears all valid status codes.
        void clear_valid_statuses() {
            valid_statuses.clear();
        }

        /// \brief Sets the User-Agent header.
        /// \param user_agent User-Agent string.
        void set_user_agent(const std::string& user_agent) {
            this->user_agent = user_agent;
        }

        /// \brief Sets the cookie data.
        /// \param cookie Cookie data as a string.
        void set_cookie(const std::string& cookie) {
            this->cookie = cookie;
        }

        /// \brief Sets the client certificate file path.
        /// \param cert_file Path to the client certificate file.
        void set_cert_file(const std::string& cert_file) {
            this->cert_file = cert_file;
        }

        /// \brief Sets the path to the CA certificate file.
        /// \param ca_file Path to the CA certificate file.
        void set_ca_file(const std::string& ca_file) {
            this->ca_file = ca_file;
        }

        /// \brief Sets the request timeout.
        /// \param timeout Timeout in seconds.
        void set_timeout(long timeout) {
            this->timeout = timeout;
        }

        /// \brief Sets the connection timeout.
        /// \param connect_timeout Connection timeout in seconds.
        void set_connect_timeout(long connect_timeout) {
            this->connect_timeout = connect_timeout;
        }

        /// \brief Enables or disables intermediate callbacks for response body chunks.
        /// \param streaming Enable (`true`) or disable (`false`) streaming callbacks.
        void set_streaming(bool streaming) {
            this->streaming = streaming;
        }

        /// \brief Sets the per-attempt response body limit, or zero for no limit.
        /// \param bytes Maximum accepted decoded body bytes, including streaming responses.
        void set_max_response_body_bytes(std::size_t bytes) {
            max_response_body_bytes = bytes;
        }

        /// \brief Enables or disables verbose mode.
        /// \param verbose Enable (`true`) or disable (`false`) verbose output.
        void set_verbose(bool verbose) {
            this->verbose = verbose;
        }

        /// \brief Enables or disables debugging headers in output.
        /// \param debug_header Enable (`true`) or disable (`false`) debug headers.
        void set_debug_header(bool debug_header) {
            this->debug_header = debug_header;
        }

    }; // HttpRequest
    
    /// \brief Owning pointer to an HTTP request.
    using HttpRequestPtr = std::unique_ptr<HttpRequest>;

}; // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_HTTP_DATA_HTTP_REQUEST_HPP_INCLUDED
