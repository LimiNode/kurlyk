#pragma once
#ifndef KURLYK_HEADER_KURLYK_WEBSOCKET_WEB_SOCKET_CLIENT_HPP_INCLUDED
#define KURLYK_HEADER_KURLYK_WEBSOCKET_WEB_SOCKET_CLIENT_HPP_INCLUDED

/// \file WebSocketClient.hpp
/// \brief Defines the WebSocketClient facade for managing WebSocket connections.

#include "WebSocketManager.hpp"
#include <mutex>

namespace kurlyk {

    /// \class WebSocketClient
    /// \brief Public facade for managing WebSocket connections, events, and message sending.
    /// Internally owns a backend-specific WebSocket client selected at compile time for the active platform.
    class WebSocketClient {
    public:

        /// \brief Initializes the WebSocket client facade and its backend-specific implementation.
        WebSocketClient() {
            ensure_initialized();
            m_client = WebSocketManager::get_instance().create_client();
            m_client->notify_handler() = []() {
                core::NetworkWorker::get_instance().notify();
            };
        }

        /// \brief Initializes the facade with a WebSocket configuration object.
        /// \param config A unique pointer to a WebSocketConfig object.
        /// \param callback Callback invoked when configuration is completed.
        WebSocketClient(std::unique_ptr<WebSocketConfig> config, std::function<void(bool)> callback = nullptr) {
            ensure_initialized();
            m_client = WebSocketManager::get_instance().create_client();
            m_client->notify_handler() = []() {
                core::NetworkWorker::get_instance().notify();
            };
            m_client->set_config(std::move(config), std::move(callback));
        }

        /// \brief Initializes the facade with URL-based configuration values.
        /// \param url The WebSocket server URL.
        /// \param headers HTTP headers included in the WebSocket connection request.
        /// \param proxy_server Proxy address in <ip:port> format.
        /// \param proxy_auth Proxy authentication in <username:password> format.
        /// \param proxy_type Proxy type (e.g., HTTP, SOCKS5).
        /// \param request_timeout Timeout for WebSocket requests in seconds (0 means no timeout).
        /// \param reconnect Enables automatic reconnection if true.
        /// \param verify_cert If true, verifies the server’s certificate and hostname according to RFC 2818.
        /// \param ca_file Path to the Root CA certificate file.
        /// \param rpm Requests per minute (RPM).
        WebSocketClient(
                const std::string& url,
                const Headers& headers = Headers(),
                const std::string& proxy_server = std::string(),
                const std::string& proxy_auth = std::string(),
                ProxyType proxy_type = ProxyType::PROXY_HTTP,
                long request_timeout = 20,
                bool reconnect = true,
                bool verify_cert = true,
                const std::string& ca_file = std::string(),
                int rpm = 200) {
            ensure_initialized();
            m_client = WebSocketManager::get_instance().create_client();
            m_client->notify_handler() = []() {
                core::NetworkWorker::get_instance().notify();
            };
            init_config();

            m_config->url = url;
            m_config->headers = headers;
            m_config->proxy_server = proxy_server;
            m_config->proxy_auth = proxy_auth;
            m_config->proxy_type = proxy_type;
            m_config->request_timeout = request_timeout;
            m_config->reconnect = reconnect;
            m_config->verify_cert = verify_cert;
            m_config->ca_file = ca_file;
            m_config->add_rate_limit(rpm, 60000);
        }

        WebSocketClient(const WebSocketClient&) = delete;
        WebSocketClient& operator=(const WebSocketClient&) = delete;

        /// \brief Schedules backend shutdown through NetworkWorker before the facade is destroyed.
        virtual ~WebSocketClient() {
            auto client = m_client;
            core::NetworkWorker::get_instance().add_task([client](){
                client->shutdown();
            });
        }

        /// \brief Sets a callback for WebSocket events.
        /// \param callback The function to be executed on each WebSocket event.
        void on_event(std::function<void(std::unique_ptr<WebSocketEventData>)> callback) {
            m_client->event_handler() = std::move(callback);
        }

        /// \brief Accessor for the event handler function.
        /// \return A reference to the event handler callback function for WebSocket events.
        std::function<void(std::unique_ptr<WebSocketEventData>)>& event_handler() {
            return m_client->event_handler();
        }

        /// \brief Asynchronously sets the WebSocket configuration.
        /// \param config A unique pointer to the WebSocket configuration object.
        /// \return A future containing the success status of the configuration setup.
        std::future<bool> set_config(std::unique_ptr<WebSocketConfig> config) {
            auto promise = std::make_shared<std::promise<bool>>();
            auto future = promise->get_future();
            m_client->set_config(std::move(config), [promise](bool success) {
                promise->set_value(success);
            });
            core::NetworkWorker::get_instance().notify();
            return future;
        }

        /// \brief Sets the WebSocket configuration and executes a callback upon completion.
        /// \param config A unique pointer to the WebSocket configuration object.
        /// \param callback The callback function to execute after setting the configuration, receiving a success status.
        void set_config(std::unique_ptr<WebSocketConfig> config, std::function<void(bool)> callback) {
            m_client->set_config(std::move(config), std::move(callback));
            core::NetworkWorker::get_instance().notify();
        }

        /// \brief Asynchronously connects to the WebSocket server.
        /// \return A future containing the success status of the connection.
        std::future<bool> connect() {
            if (m_config) {
#               if __cplusplus >= 201402L
                auto config = std::make_unique<WebSocketConfig>(*m_config.get());
#               else
                auto config = std::unique_ptr<WebSocketConfig>(new WebSocketConfig(*m_config.get()));
#               endif
                m_client->set_config(std::move(config), nullptr);
            }
            auto promise = std::make_shared<std::promise<bool>>();
            auto future = promise->get_future();
            m_client->connect([promise](bool success) {
                promise->set_value(success);
            });
            core::NetworkWorker::get_instance().notify();
            return future;
        }

        /// \brief Connects to the WebSocket server and executes a callback upon completion.
        /// \param callback The callback function to execute after connection, receiving a success status.
        void connect(std::function<void(bool)> callback) {
            if (m_config) {
#               if __cplusplus >= 201402L
                auto config = std::make_unique<WebSocketConfig>(*m_config.get());
#               else
                auto config = std::unique_ptr<WebSocketConfig>(new WebSocketConfig(*m_config.get()));
#               endif
                m_client->set_config(std::move(config), nullptr);
            }
            m_client->connect(std::move(callback));
            core::NetworkWorker::get_instance().notify();
        }

        /// \brief Connects to the WebSocket server, blocking until the connection completes.
        /// \return True if the connection was successful, or false if it failed.
        bool connect_and_wait() {
            auto connect_future = connect();
            return connect_future.get();
        }

        /// \brief Asynchronously disconnects from the WebSocket server.
        /// \return A future containing the success status of the disconnection.
        std::future<bool> disconnect() {
            auto promise = std::make_shared<std::promise<bool>>();
            auto future = promise->get_future();
            m_client->disconnect([promise](bool success) {
                promise->set_value(success);
            });
            core::NetworkWorker::get_instance().notify();
            return future;
        }

        /// \brief Disconnects from the WebSocket server, blocking until the disconnection completes.
        /// \return True if the disconnection was successful, or false if it failed.
        bool disconnect_and_wait() {
            auto disconnect_future = disconnect();
            return disconnect_future.get();
        }

        /// \brief Disconnects from the WebSocket server and invokes a callback upon completion.
        /// \param callback The callback function to execute after disconnection, receiving a success status.
        void disconnect(std::function<void(bool success)> callback) {
            m_client->disconnect(std::move(callback));
            core::NetworkWorker::get_instance().notify();
        }

        /// \brief Checks if the WebSocket is connected.
        /// \return True if the WebSocket is connected, false otherwise.
        bool is_connected() const {
            return m_client->is_connected();
        }

        /// \brief Attempts to submit a message through the WebSocket.
        /// \param message The content of the message to be sent.
        /// \param rate_limit_id The ID of the rate limit to apply to this message. A value of 0 indicates the default or no rate limit.
        /// \param callback An optional callback to execute after sending the message.
        /// \return SubmitResult describing whether the message was successfully queued.
        SubmitResult submit_message(
                const std::string &message,
                long rate_limit_id = 0,
                std::function<void(const std::error_code&)> callback = nullptr) {
            return m_client->submit_message(message, rate_limit_id, std::move(callback));
        }

        /// \brief Sends a message through the WebSocket.
        /// \param message The content of the message to be sent.
        /// \param rate_limit_id The ID of the rate limit to apply to this message. A value of 0 indicates the default or no rate limit.
        /// \param callback An optional callback to execute after sending the message.
        /// \return True if the message was successfully queued, false otherwise.
        bool send_message(
                const std::string &message,
                long rate_limit_id = 0,
                std::function<void(const std::error_code&)> callback = nullptr) {
            return submit_message(message, rate_limit_id, std::move(callback)).accepted;
        }

        /// \brief Attempts to submit a close request to the WebSocket server.
        /// \param status The status code for the close request (default: 1000).
        /// \param reason Optional reason for closing the connection.
        /// \param callback Optional callback to execute after sending the close request.
        /// \return SubmitResult describing whether the close request was successfully queued.
        SubmitResult submit_close(
                const int status = 1000,
                const std::string &reason = std::string(),
                std::function<void(const std::error_code&)> callback = nullptr) {
            return m_client->submit_close(status, reason, std::move(callback));
        }

        /// \brief Sends a close request to the WebSocket server.
        /// \param status The status code for the close request (default: 1000).
        /// \param reason Optional reason for closing the connection.
        /// \param callback Optional callback to execute after sending the close request.
        /// \return True if the close request was successfully queued, false otherwise.
        bool send_close(
                const int status = 1000,
                const std::string &reason = std::string(),
                std::function<void(const std::error_code&)> callback = nullptr) {
            return submit_close(status, reason, std::move(callback)).accepted;
        }

        /// \brief Retrieves all pending WebSocket events in a batch.
        /// \return A list of unique pointers to WebSocketEventData objects representing events.
        std::list<std::unique_ptr<WebSocketEventData>> receive_events() {
            return m_client->receive_events();
        }

        /// \brief Retrieves a single WebSocket event, if available.
        /// \return A unique pointer to a WebSocketEventData object representing a single event, or nullptr if no events are available.
        std::unique_ptr<WebSocketEventData> receive_event() {
            return m_client->receive_event();
        }

        /// \brief Retrieves the HTTP version used in the WebSocket connection.
        /// \return The HTTP version string.
        std::string get_http_version() const {
            return m_client->get_http_version();
        }

        /// \brief Retrieves the headers associated with the WebSocket connection.
        /// \return A Headers object containing the HTTP headers.
        Headers get_headers() const {
            return m_client->get_headers();
        }

        /// \brief Retrieves the remote endpoint information.
        /// \return The remote endpoint as a string in the format "IP:Port".
        std::string get_remote_endpoint() const {
            return m_client->get_remote_endpoint();
        }

        /// \brief Sets the WebSocket server URL with optional query parameters.
        /// \param host Hostname or IP address.
        /// \param path Path for the request.
        /// \param query Optional query parameters.
        void set_url(const std::string& host, const std::string& path, const std::string& query = "") {
            init_config();
            return m_config->set_url(host, path, query);
        }

        /// \brief Sets the WebSocket server URL with specified query parameters.
        /// \param url Full URL of the server.
        /// \param query Query parameters as a dictionary.
        void set_url(const std::string& url, const QueryParams& query) {
            init_config();
            return m_config->set_url(url, query);
        }

        /// \brief Sets the Accept-Encoding header with specified encodings.
        /// \param identity Enables identity encoding.
        /// \param deflate Enables deflate encoding.
        /// \param gzip Enables gzip encoding.
        /// \param brotli Enables brotli encoding.
        void set_accept_encoding(bool identity = false, bool deflate = false, bool gzip = false, bool brotli = false) {
            init_config();
            return m_config->set_accept_encoding(identity, deflate, gzip, brotli);
        }

        /// \brief Sets the proxy server address.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        /// \param type Type of proxy (default is HTTP).
        void set_proxy(
                const std::string& ip,
                int port,
                ProxyType type = ProxyType::PROXY_HTTP) {
            init_config();
            return m_config->set_proxy(ip, port, type);
        }

        /// \brief Sets the proxy server address with authentication.
        /// \param ip Proxy server IP address.
        /// \param port Proxy server port.
        /// \param username Proxy username.
        /// \param password Proxy password.
        /// \param type Type of proxy (default is HTTP).
        void set_proxy(
                const std::string& ip,
                int port,
                const std::string& username,
                const std::string& password,
                ProxyType type = ProxyType::PROXY_HTTP) {
            init_config();
            return m_config->set_proxy(ip, port, username, password, type);
        }

        /// \brief Sets the proxy server address.
        /// \param server Proxy address in <ip:port> format.
        void set_proxy_server(const std::string& server) {
            init_config();
            return m_config->set_proxy_server(server);
        }

        /// \brief Sets the proxy authentication credentials.
        /// \param auth Proxy authentication in <username:password> format.
        void set_proxy_auth(const std::string& auth) {
            init_config();
            return m_config->set_proxy_auth(auth);
        }

        /// \brief Sets the proxy type.
        /// \param type Type of proxy.
        void set_proxy_type(ProxyType type) {
            init_config();
            return m_config->set_proxy_type(type);
        }

        /// \brief Configures proxy authentication credentials.
        /// \param username Proxy username.
        /// \param password Proxy password.
        void set_proxy_auth(const std::string& username, const std::string& password) {
            init_config();
            return m_config->set_proxy_auth(username, password);
        }

        /// \brief Configures reconnection behavior.
        /// \param reconnect Enables automatic reconnection.
        /// \param reconnect_attempts Number of reconnection attempts (0 means infinite attempts).
        /// \param reconnect_delay Delay in seconds between reconnection attempts.
        void set_reconnect(bool reconnect, long reconnect_attempts = 0, long reconnect_delay = 0) {
            init_config();
            return m_config->set_reconnect(reconnect, reconnect_attempts, reconnect_delay);
        }

        /// \brief Sets the User-Agent header.
        /// \param user_agent User-Agent string.
        void set_user_agent(const std::string& user_agent) {
            init_config();
            return m_config->set_user_agent(user_agent);
        }

        /// \brief Sets the Accept-Language header.
        /// \param accept_language Accept-Language string.
        void set_accept_language(const std::string& accept_language) {
            init_config();
            return m_config->set_accept_language(accept_language);
        }

        /// \brief Sets the cookie data.
        /// \param cookie Cookie data string.
        void set_cookie(const std::string& cookie) {
            init_config();
            return m_config->set_cookie(cookie);
        }

        /// \brief Configures the idle timeout for the WebSocket connection.
        /// \param idle_timeout Idle timeout in seconds (0 means no timeout).
        void set_idle_timeout(long idle_timeout) {
            init_config();
            return m_config->set_idle_timeout(idle_timeout);
        }

        /// \brief Sets the timeout for WebSocket requests.
        /// \param request_timeout Request timeout in seconds (0 means no timeout).
        void set_request_timeout(long request_timeout) {
            init_config();
            return m_config->set_request_timeout(request_timeout);
        }

        /// \brief Sets the maximum number of outbound send operations queued for this client.
        /// \param max_send_queue_size Queue limit, or `0` to keep the send queue unbounded.
        void set_max_send_queue_size(std::size_t max_send_queue_size) {
            init_config();
            return m_config->set_max_send_queue_size(max_send_queue_size);
        }

        /// \brief Sets the path to the CA certificate file.
        /// \param ca_file Path to the CA certificate file.
        void set_ca_file(const std::string& ca_file) {
            init_config();
            return m_config->set_ca_file(ca_file);
        }

        /// \brief Sets certificate verification and sets the CA certificate file.
        /// \param verify_cert If true, enables server certificate verification.
        /// \param ca_file Path to the CA certificate file.
        void set_ca_file(bool verify_cert, const std::string& ca_file) {
            init_config();
            return m_config->set_ca_file(verify_cert, ca_file);
        }

        /// \brief Sets whether to verify the server’s certificate.
        /// \param verify_cert If true, enables server certificate verification.
        void set_verify_cert(bool verify_cert) {
            init_config();
            return m_config->set_verify_cert(verify_cert);
        }

        /// \brief Adds a rate limit configuration to control the frequency of WebSocket messages.
        ///
        /// The first rate limit added will serve as the primary rate limit, applying to all WebSocket
        /// requests by default. Additional rate limits can be added for specific types of requests
        /// or contexts as needed.
        ///
        /// \param requests_per_period The maximum number of messages allowed within the specified period.
        /// \param period_ms The time period in milliseconds during which the request limit applies.
        /// \return The index of the added rate limit configuration.
        long add_rate_limit(long requests_per_period, long period_ms) {
            init_config();
            return m_config->add_rate_limit(requests_per_period, period_ms);
        }

        /// \brief Adds a rate limit based on Requests Per Minute (RPM).
        /// \param requests_per_minute Maximum number of requests allowed per minute.
        /// \return The index of the added rate limit configuration.
        long add_rate_limit_rpm(long requests_per_minute) {
            long period_ms = 60000; // 1 minute in milliseconds
            return add_rate_limit(requests_per_minute, period_ms);
        }

        /// \brief Adds a rate limit based on Requests Per Second (RPS).
        /// \param requests_per_second Maximum number of requests allowed per second.
        /// \return The index of the added rate limit configuration.
        long add_rate_limit_rps(long requests_per_second) {
            long period_ms = 1000; // 1 second in milliseconds
            return add_rate_limit(requests_per_second, period_ms);
        }

    private:
        selected_backend_client_ptr m_client; ///< Backend-specific WebSocket client selected by compile-time platform macros.
        std::unique_ptr<WebSocketConfig> m_config; ///< Deferred WebSocket configuration copied into the backend before connect().

        /// \brief Initializes the WebSocket configuration if it is not already set.
        void init_config() {
            if (!m_config) {
#               if __cplusplus >= 201402L
                m_config = std::make_unique<WebSocketConfig>();
#               else
                m_config = std::unique_ptr<WebSocketConfig>(new WebSocketConfig());
#               endif
            }
        }

        /// \brief Ensures the WebSocket and network components are initialized.
        /// This method is called only once per application run.
        static void ensure_initialized() {
            static std::once_flag once;
            std::call_once(once, []() {
#               if KURLYK_HTTP_SUPPORT
                HttpRequestManager::get_instance();
#               endif
                WebSocketManager::get_instance();
                core::NetworkWorker::get_instance().start(KURLYK_AUTO_INIT_USE_ASYNC);
            });
        }

    }; // WebSocketClient

} // namespace kurlyk

#endif // KURLYK_HEADER_KURLYK_WEBSOCKET_WEB_SOCKET_CLIENT_HPP_INCLUDED
